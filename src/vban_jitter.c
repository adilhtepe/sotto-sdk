/* VBAN SDK - receive jitter buffer, playout-deadline design (ADR-001).
 * See vban/vban_jitter.h for the model.
 *
 * Concurrency (SPSC, no locks):
 *  - producer owns: ring writes, tags[], w, sh_cur/sh_prev, epoch, late_ev
 *  - consumer owns: r and all playout state
 *  - a packet's samples are written before its tag is stored (release); the
 *    consumer reads samples only after loading a matching tag (acquire)
 *  - the producer never writes positions before r (already played or being
 *    played) nor more than `window` frames ahead of r, so a ring position is
 *    never rewritten while it can still be read
 *  - anchors are published under a seqlock (all fields acquire/release); the
 *    producer never waits, the consumer keeps its old view and retries on its
 *    next pull if it raced with an update
 */
#include "vban/vban_jitter.h"
#include "vban/vban_pcm.h"
#include <string.h>

#define FADE_FRAMES    64      /* fade-in after concealment */
#define DECAY          0.95f   /* per-frame decay of concealment (about 1 ms to -20 dB @48k) */
#define DRIFT_GAIN     0.004f  /* ratio change per unit of relative fill error */
#define DRIFT_LIMIT    0.005f  /* max +-0.5 % speed correction */
#define STABLE_SECONDS 4u      /* adaptive shrink window */
#define TAG_MASK       (VBAN_JB_TAGS - 1u)

#if (VBAN_JB_TAGS & (VBAN_JB_TAGS - 1)) || VBAN_JB_TAGS < 16
#error "VBAN_JB_TAGS must be a power of two >= 16"
#endif

static int is_pow2(uint32_t v) { return v && !(v & (v - 1)); }
static inline uint32_t tag_value(uint32_t frame, uint32_t epoch) { return frame ^ (epoch * 2654435761u); }

size_t vban_jb_storage_bytes(uint32_t cap, unsigned ch, unsigned storage)
{
    return (size_t)cap * ch * (storage == VBAN_JB_S16 ? 2u : 4u);
}

int vban_jb_init(vban_jb_t *jb, const vban_jb_config_t *cfg, void *storage, uint32_t cap)
{
    vban_jb_config_t def;
    unsigned i;
    if (!jb || !storage || !is_pow2(cap) || cap < 256) return VBAN_E_ARG;
    if (!cfg) {
        memset(&def, 0, sizeof def);
        def.channels = 2;
        def.adaptive = 1;
        cfg = &def;
    }
    if (cfg->channels == 0 || cfg->storage > VBAN_JB_S16 || cfg->drift > VBAN_JB_DRIFT_NONE)
        return VBAN_E_ARG;
    memset(jb, 0, sizeof *jb);
    jb->ring     = storage;
    jb->mask     = cap - 1;
    jb->channels = cfg->channels;
    jb->storage  = cfg->storage;
    jb->drift    = cfg->drift;
    jb->adaptive = cfg->adaptive;
    jb->out_rate = cfg->out_rate;
    jb->max_frames = cfg->max_frames ? cfg->max_frames : cap / 4 * 3;
    if (jb->max_frames > cap - 2 * VBAN_SAMPLES_MAX) jb->max_frames = cap - 2 * VBAN_SAMPLES_MAX;
    if (jb->max_frames < 64) jb->max_frames = 64;
    jb->min_frames = cfg->min_frames ? cfg->min_frames : 64;
    if (jb->min_frames > jb->max_frames) jb->min_frames = jb->max_frames;
    jb->target = cfg->target_frames ? cfg->target_frames : 2 * VBAN_SAMPLES_MAX;
    if (jb->target > jb->max_frames) jb->target = jb->max_frames;
    if (jb->target < jb->min_frames) jb->target = jb->min_frames;
    jb->ratio = 1.0f;
    jb->min_fill = 0xFFFFFFFFu;
    jb->pkt_frames = VBAN_SAMPLES_MAX;
    jb->conc_gain = 1.0f;
    for (i = 0; i < VBAN_JB_TAGS; ++i) jb->tags[i] = 0xA5A5A5A5u ^ (i * 0x01000193u); /* never a live tag in practice */
    vban_seq_init(&jb->seq, cfg->max_gap ? cfg->max_gap : 32);
    memset(storage, 0, vban_jb_storage_bytes(cap, cfg->channels, cfg->storage));
    return VBAN_OK;
}

/* ================================================================ ring access */

static inline float ring_get(const vban_jb_t *jb, uint32_t frame, unsigned c)
{
    size_t i = (size_t)(frame & jb->mask) * jb->channels + c;
    return jb->storage == VBAN_JB_F32 ? ((const float *)jb->ring)[i]
                                      : ((const int16_t *)jb->ring)[i] * (1.0f / 32768.0f);
}

static inline void ring_set(vban_jb_t *jb, uint32_t frame, unsigned c, float v)
{
    size_t i = (size_t)(frame & jb->mask) * jb->channels + c;
    if (jb->storage == VBAN_JB_F32) {
        ((float *)jb->ring)[i] = v;
    } else {
        float s = v * 32768.0f;
        s = s >= 32767.0f ? 32767.0f : (s >= -32768.0f ? s : -32768.0f);
        ((int16_t *)jb->ring)[i] = (int16_t)(s + (s >= 0 ? 0.5f : -0.5f));
    }
}

/* ================================================================ producer */

/* Decode frames [skip, n) of a packet into the ring at position `w`. */
static int write_frames(vban_jb_t *jb, uint32_t w, const vban_packet_t *p, uint32_t skip)
{
    unsigned ch = jb->channels, src_ch = p->channels, c;
    uint32_t n = p->samples - skip, idx = w & jb->mask, n1 = jb->mask + 1 - idx, f;
    size_t fb = (size_t)src_ch * p->sample_size;
    const uint8_t *d = p->data + skip * fb;

    if (src_ch == ch) {
        int r;
        if (n1 > n) n1 = n;
        if (jb->storage == VBAN_JB_F32) {
            float *ring = (float *)jb->ring;
            r = vban_pcm_to_f32(d, p->datatype, ring + (size_t)idx * ch, (size_t)n1 * ch);
            if (r == VBAN_OK && n > n1) r = vban_pcm_to_f32(d + n1 * fb, p->datatype, ring, (size_t)(n - n1) * ch);
        } else {
            int16_t *ring = (int16_t *)jb->ring;
            r = vban_pcm_to_s16(d, p->datatype, ring + (size_t)idx * ch, (size_t)n1 * ch);
            if (r == VBAN_OK && n > n1) r = vban_pcm_to_s16(d + n1 * fb, p->datatype, ring, (size_t)(n - n1) * ch);
        }
        return r;
    }
    /* channel remap: mono -> every channel, missing -> silence, extra -> dropped */
    for (f = 0; f < n; ++f) {
        const uint8_t *fr = d + f * fb;
        for (c = 0; c < ch; ++c) {
            unsigned sc = src_ch == 1 ? 0 : c;
            float v = 0.0f;
            if (sc < src_ch) vban_pcm_to_f32(fr + sc * p->sample_size, p->datatype, &v, 1);
            ring_set(jb, w + f, c, v);
        }
    }
    return VBAN_OK;
}

static void put_anchor(vban_jb_anchor_sh_t *d, const vban_jb_anchor_t *a)
{
    vban_store_rel(&d->pos, a->pos);
    vban_store_rel(&d->frame, a->frame);
    vban_store_rel(&d->nbs, a->nbs);
    vban_store_rel(&d->epoch, a->epoch);
    vban_store_rel(&d->valid, a->valid);
}

static void get_anchor(vban_jb_anchor_t *a, const vban_jb_anchor_sh_t *s)
{
    a->pos = vban_load_acq(&s->pos);
    a->frame = vban_load_acq(&s->frame);
    a->nbs = vban_load_acq(&s->nbs);
    a->epoch = vban_load_acq(&s->epoch);
    a->valid = vban_load_acq(&s->valid);
}

/* Publish a new anchor so that frame `f` lands at `pos` (keeps the previous
 * one so audio already buffered under it stays playable). */
static void set_anchor(vban_jb_t *jb, uint32_t pos, uint32_t f, uint32_t nbs)
{
    vban_jb_anchor_t prev = jb->cur;
    uint32_t s = jb->epoch_val * 2u;
    jb->cur.pos = pos;
    jb->cur.frame = f;
    jb->cur.nbs = nbs;
    jb->cur.epoch = ++jb->epoch_val;
    jb->cur.valid = 1;
    vban_store_rel(&jb->aseq, s + 1u);       /* odd: update in progress */
    put_anchor(&jb->sh_prev, &prev);
    put_anchor(&jb->sh_cur, &jb->cur);
    vban_store_rel(&jb->aseq, s + 2u);
}

int vban_jb_push(vban_jb_t *jb, const vban_packet_t *p)
{
    uint32_t r, cap, lost = 0, n, f, pos, end, win, skip, tv;
    int32_t d;
    vban_seq_result_t sr;
    if (!jb || !p) return VBAN_E_ARG;
    if (p->protocol != VBAN_PROTOCOL_AUDIO || p->codec != VBAN_CODEC_PCM || p->sample_size == 0 ||
        p->sample_rate == 0 || !p->valid)
        return VBAN_E_UNSUPPORTED;

    {   /* sample-rate change: needs VBAN_JB_RATE_CONFIRM agreeing packets in a row */
        uint32_t cur = vban_load_rlx(&jb->in_rate);
        if (cur && p->sample_rate != cur) {
            if (jb->rate_cand == p->sample_rate) jb->rate_cnt++;
            else { jb->rate_cand = p->sample_rate; jb->rate_cnt = 1; }
            if (jb->rate_cnt < VBAN_JB_RATE_CONFIRM) { jb->rejected++; return VBAN_OK; }
            vban_seq_init(&jb->seq, jb->seq.max_gap);  /* new stream format: resync */
            jb->force_anchor = 1;
        }
        jb->rate_cnt = 0;
        jb->rate_cand = 0;
    }

    f = p->nu_frame;
    n = p->samples;
    sr = vban_seq_update(&jb->seq, f, &lost);
    if (sr == VBAN_SEQ_SUSPECT) { jb->rejected++; return VBAN_OK; }

    cap = jb->mask + 1;
    r = vban_load_acq(&jb->r);

    if (!jb->cur.valid || sr == VBAN_SEQ_FIRST || sr == VBAN_SEQ_RESET || n != jb->cur.nbs || jb->force_anchor) {
        /* continue right after the newest audio (or at the playout point) */
        uint32_t at = (int32_t)(jb->w_own - r) > 0 ? jb->w_own : r;
        set_anchor(jb, at, f, n);
        if (sr == VBAN_SEQ_RESET || jb->force_anchor) jb->resets++;
        jb->force_anchor = 0;
    }

    vban_store_rlx(&jb->pkt_frames, n);
    if (vban_load_rlx(&jb->in_rate) != p->sample_rate) vban_store_rel(&jb->in_rate, p->sample_rate);

    d = (int32_t)(f - jb->cur.frame);
    if (d < 0) { jb->late++; return VBAN_OK; }           /* older than the current anchor */
    win = (VBAN_JB_TAGS - 1u) * n;
    if (win > cap) win = cap;
    pos = jb->cur.pos + (uint32_t)d * n;
    end = pos + n;

    /* a copy of a packet we already have (played or not) is a duplicate, not a
     * late packet: redundant streams must not inflate the latency target */
    tv = tag_value(f, jb->cur.epoch);
    if (vban_load_rlx(&jb->tags[f & TAG_MASK]) == tv) { jb->dup++; return VBAN_OK; }

    if ((int32_t)(end - r) <= 0) {                        /* its playout time has passed */
        jb->late++;
        vban_store_rel(&jb->late_ev, vban_load_rlx(&jb->late_ev) + 1);
        return VBAN_OK;
    }
    if (end - r > win) { jb->overruns++; return VBAN_E_NOSPACE; } /* too far ahead to keep */

    skip = (int32_t)(pos - r) < 0 ? r - pos : 0;          /* partly late: keep the future part */
    if (write_frames(jb, pos + skip, p, skip) != VBAN_OK) return VBAN_E_UNSUPPORTED;
    vban_store_rel(&jb->tags[f & TAG_MASK], tv);

    jb->packets++;
    if (sr == VBAN_SEQ_GAP) jb->lost += lost;
    if (sr == VBAN_SEQ_LATE) jb->repaired++;
    if (skip) vban_store_rel(&jb->late_ev, vban_load_rlx(&jb->late_ev) + 1);
    if ((int32_t)(end - jb->w_own) > 0) {
        jb->w_own = end;
        vban_store_rel(&jb->w, end);
    }
    return VBAN_OK;
}

/* ================================================================ consumer */

static void sync_anchors(vban_jb_t *jb)
{
    vban_jb_anchor_t c, p;
    uint32_t s1 = vban_load_acq(&jb->aseq), s2;
    if (s1 == jb->seen_seq || (s1 & 1u)) return;   /* unchanged, or being written: next pull */
    get_anchor(&c, &jb->sh_cur);
    get_anchor(&p, &jb->sh_prev);
    s2 = vban_load_acq(&jb->aseq);
    if (s2 != s1) return;                            /* raced with an update: retry later */
    jb->c_cur = c;
    jb->c_prev = p;
    jb->seen_seq = s1;
}

/* Is the frame at ring position `idx` present? (w = newest position seen) */
static int frame_valid(const vban_jb_t *jb, uint32_t idx, uint32_t w)
{
    const vban_jb_anchor_t *a = &jb->c_cur;
    uint32_t k, f;
    if ((int32_t)(w - idx) <= 0) return 0;
    if (!a->valid || (int32_t)(idx - a->pos) < 0) {
        a = &jb->c_prev;
        if (!a->valid || (int32_t)(idx - a->pos) < 0) return 0;
    }
    k = (idx - a->pos) / a->nbs;
    f = a->frame + k;
    return vban_load_acq(&jb->tags[f & TAG_MASK]) == tag_value(f, a->epoch);
}

/* Are all frames in [idx, idx + n) present? Checks one tag per packet. */
static int range_valid(const vban_jb_t *jb, uint32_t idx, uint32_t n, uint32_t w)
{
    uint32_t end = idx + n;
    if ((int32_t)(w - end) < 0) return 0;
    while ((int32_t)(end - idx) > 0) {
        const vban_jb_anchor_t *a = &jb->c_cur;
        uint32_t next;
        if (!frame_valid(jb, idx, w)) return 0;
        if (!a->valid || (int32_t)(idx - a->pos) < 0) a = &jb->c_prev;
        next = a->pos + ((idx - a->pos) / a->nbs + 1) * a->nbs;  /* start of the next packet */
        if (a == &jb->c_prev && jb->c_cur.valid && (int32_t)(next - jb->c_cur.pos) > 0) next = jb->c_cur.pos;
        idx = next;
    }
    return 1;
}

/* Plain copy of n frames starting at frame r. */
static void copy_out(const vban_jb_t *jb, uint32_t r, float *out, uint32_t n)
{
    unsigned ch = jb->channels;
    uint32_t idx = r & jb->mask, n1 = jb->mask + 1 - idx;
    if (n1 > n) n1 = n;
    if (jb->storage == VBAN_JB_F32) {
        const float *ring = (const float *)jb->ring;
        memcpy(out, ring + (size_t)idx * ch, (size_t)n1 * ch * sizeof(float));
        if (n > n1) memcpy(out + (size_t)n1 * ch, ring, (size_t)(n - n1) * ch * sizeof(float));
    } else {
        const int16_t *ring = (const int16_t *)jb->ring;
        size_t i, k = (size_t)n1 * ch;
        for (i = 0; i < k; ++i) out[i] = ring[(size_t)idx * ch + i] * (1.0f / 32768.0f);
        for (i = 0; i < (size_t)(n - n1) * ch; ++i) out[k + i] = ring[i] * (1.0f / 32768.0f);
    }
}

/* Linear-interpolating read of n output frames; returns input frames consumed. */
static uint32_t resample_out(vban_jb_t *jb, uint32_t r, float *out, uint32_t n, float ratio)
{
    unsigned ch = jb->channels, c;
    uint32_t i, idx = r, mask = jb->mask;
    float pos = jb->frac;
    if (jb->storage == VBAN_JB_F32) {
        const float *ring = (const float *)jb->ring;
        for (i = 0; i < n; ++i) {
            const float *a = ring + (size_t)(idx & mask) * ch;
            const float *b = ring + (size_t)((idx + 1) & mask) * ch;
            for (c = 0; c < ch; ++c) out[c] = a[c] + (b[c] - a[c]) * pos;
            out += ch;
            pos += ratio;
            while (pos >= 1.0f) { pos -= 1.0f; ++idx; }
        }
    } else {
        const int16_t *ring = (const int16_t *)jb->ring;
        for (i = 0; i < n; ++i) {
            const int16_t *a = ring + (size_t)(idx & mask) * ch;
            const int16_t *b = ring + (size_t)((idx + 1) & mask) * ch;
            for (c = 0; c < ch; ++c) out[c] = (a[c] + (b[c] - a[c]) * pos) * (1.0f / 32768.0f);
            out += ch;
            pos += ratio;
            while (pos >= 1.0f) { pos -= 1.0f; ++idx; }
        }
    }
    jb->frac = pos;
    return idx - r;
}

/* One concealment frame: decay from the last real frame toward silence. */
static void conceal_frame(vban_jb_t *jb, float *o)
{
    unsigned ch = jb->channels, c;
    jb->conc_gain *= DECAY;
    for (c = 0; c < ch; ++c) o[c] = c < VBAN_JB_PLC_CH ? jb->last_good[c] * jb->conc_gain : 0.0f;
    jb->concealing = 1;
    jb->concealed++;
}

static void remember_last(vban_jb_t *jb, const float *frame)
{
    unsigned c, ch = jb->channels < VBAN_JB_PLC_CH ? jb->channels : VBAN_JB_PLC_CH;
    for (c = 0; c < ch; ++c) jb->last_good[c] = frame[c];
    jb->conc_gain = 1.0f;
}

/* Frame-by-frame playout around holes. Returns input frames consumed. */
static uint32_t slow_out(vban_jb_t *jb, uint32_t r, uint32_t w, float *out, uint32_t n, uint32_t pkt)
{
    unsigned ch = jb->channels, c;
    uint32_t i, idx = r;
    for (i = 0; i < n; ++i, out += ch) {
        if (frame_valid(jb, idx, w)) {
            if (jb->stalled) {
                /* resume after starvation only with a pull's worth buffered, or
                 * we would starve again on the very next frames */
                if ((int32_t)(w - idx) < (int32_t)(jb->max_pull + pkt)) {
                    conceal_frame(jb, out);
                    jb->stall_frames++;
                    continue;
                }
                jb->stalled = 0;
            }
            for (c = 0; c < ch; ++c) out[c] = ring_get(jb, idx, c);
            if (jb->concealing) { jb->concealing = 0; jb->fade_left = FADE_FRAMES; }
            if (jb->fade_left) {
                float g = 1.0f - (float)jb->fade_left / (float)(FADE_FRAMES + 1);
                for (c = 0; c < ch; ++c) out[c] *= g;
                jb->fade_left--;
            }
            remember_last(jb, out);
            ++idx;
        } else if ((int32_t)(w - idx) > 0) {
            conceal_frame(jb, out);  /* loss: newer audio exists, keep the clock running */
            ++idx;
        } else {
            if (!jb->stalled) {      /* starvation: hold playout until audio arrives */
                jb->stalled = 1;
                jb->underruns++;
                jb->stable_frames = 0;
                jb->min_fill = 0xFFFFFFFFu;
                if (jb->adaptive) {
                    uint32_t grow = pkt > 32 ? pkt : 32;
                    jb->target = jb->target + grow > jb->max_frames ? jb->max_frames : jb->target + grow;
                }
            }
            conceal_frame(jb, out);
            jb->stall_frames++;
        }
    }
    jb->frac = 0.0f;
    return idx - r;
}

static float stream_rate(const vban_jb_t *jb)
{
    uint32_t in = vban_load_rlx(&jb->in_rate);
    return in ? (float)in : 48000.0f;
}

static size_t pull(vban_jb_t *jb, float *out, uint32_t frames)
{
    unsigned ch = jb->channels;
    uint32_t w, r = jb->r, used, pkt;
    int32_t avail, fill;
    float nominal = 1.0f, rate;
    int resample;

    if (vban_load_acq(&jb->reset_req)) {
        vban_store_rlx(&jb->reset_req, 0);
        r = vban_load_acq(&jb->w);
        jb->playing = 0;
        jb->stalled = 0;
        jb->frac = 0.0f;
        vban_store_rel(&jb->r, r);
    }
    sync_anchors(jb);

    w = vban_load_acq(&jb->w);
    avail = (int32_t)(w - r);
    pkt = vban_load_rlx(&jb->pkt_frames);
    rate = stream_rate(jb);
    if (jb->out_rate) nominal = rate / (float)jb->out_rate;

    /* A target below pull + packet frames cannot be met: just before a packet
     * lands the buffer holds ~target - packet frames, and the pull needs
     * `frames`. netsim (wired LAN, 30 ppm drift, pull 128, packet 64) showed 6
     * underruns / 2 min with the target at 64 and 0 with this floor. */
    if (frames > jb->max_pull) jb->max_pull = frames;
    if (jb->adaptive) {
        uint32_t floor_ = jb->max_pull + pkt, lim = (VBAN_JB_TAGS - 2u) * pkt;
        if (floor_ < jb->min_frames) floor_ = jb->min_frames;
        if (floor_ > jb->max_frames) floor_ = jb->max_frames;
        if (jb->target < floor_) jb->target = floor_;
        if (jb->target > lim && lim >= floor_) jb->target = lim;  /* slot window */
    }

    if (!jb->playing) {
        if (!jb->c_cur.valid || avail < (int32_t)jb->target || avail <= 0) {
            memset(out, 0, (size_t)frames * ch * sizeof(float));
            return 0;
        }
        jb->playing = 1;
        jb->stalled = 0;
        jb->frac = 0.0f;
        jb->fill_avg = (float)avail;
        jb->stall_frames = 0;
        if (jb->drift != VBAN_JB_DRIFT_RESAMPLE) jb->ratio = nominal;
    }

    /* late packets were dropped: playout ran ahead of the network */
    {
        uint32_t le = vban_load_acq(&jb->late_ev);
        if (le != jb->seen_late) {
            jb->seen_late = le;
            if (jb->adaptive) {
                uint32_t grow = pkt > 32 ? pkt : 32;
                jb->target = jb->target + grow > jb->max_frames ? jb->max_frames : jb->target + grow;
            }
            jb->stable_frames = 0;
            jb->min_fill = 0xFFFFFFFFu;
        }
    }

    resample = jb->ratio != 1.0f || jb->frac != 0.0f || nominal != 1.0f ||
               jb->drift == VBAN_JB_DRIFT_RESAMPLE;
    {
        uint32_t need = resample ? (uint32_t)(jb->frac + (float)frames * jb->ratio) + 2 : frames;
        if (!jb->stalled && !jb->concealing && !jb->fade_left && range_valid(jb, r, need, w)) {
            /* ---- fast path: everything present ---- */
            if (resample) {
                used = resample_out(jb, r, out, frames, jb->ratio);
            } else {
                used = frames;
                if (jb->drift == VBAN_JB_DRIFT_SLIP && frames > 1) {
                    float thr = (float)(pkt > 32 ? pkt / 2 : 16);
                    if (jb->fill_avg > (float)jb->target + thr && avail > (int32_t)frames + 1 &&
                        range_valid(jb, r, frames + 1, w)) {
                        copy_out(jb, r + 1, out, frames);     /* drop one frame */
                        used = frames + 1;
                        jb->fill_avg -= 1.0f;
                    } else if (jb->fill_avg < (float)jb->target - thr) {
                        copy_out(jb, r, out, frames - 1);     /* repeat one frame */
                        memcpy(out + (size_t)(frames - 1) * ch, out + (size_t)(frames - 2) * ch, ch * sizeof(float));
                        used = frames - 1;
                        jb->fill_avg += 1.0f;
                    } else {
                        copy_out(jb, r, out, frames);
                    }
                } else {
                    copy_out(jb, r, out, frames);
                }
            }
            remember_last(jb, out + (size_t)(frames - 1) * ch);
        } else {
            /* ---- slow path: holes, starvation, fade-in ---- */
            used = slow_out(jb, r, w, out, frames, pkt);
        }
    }
    r += used;

    /* stream stopped: give up after holding for the whole buffer range */
    if (jb->stalled && jb->stall_frames > jb->max_frames + frames) {
        jb->playing = 0;
        jb->stalled = 0;
        jb->stall_frames = 0;
        vban_store_rel(&jb->r, r);
        return frames;
    }
    if (!jb->stalled) jb->stall_frames = 0;

    fill = (int32_t)(w - r);
    if (fill < 0) fill = 0;

    /* ---- smoothed fill level (time constant ~0.5 s) ---- */
    {
        float alpha = (float)frames / (rate * 0.5f);
        if (alpha > 1.0f) alpha = 1.0f;
        jb->fill_avg += ((float)fill - jb->fill_avg) * alpha;
    }

    /* ---- way too full (burst after a network stall): jump forward ---- */
    {
        uint32_t slack = jb->target > 2 * pkt ? jb->target : 2 * pkt;
        if ((uint32_t)fill > jb->target + slack + frames) {
            r = w - jb->target;
            fill = (int32_t)jb->target;
            jb->fill_avg = (float)fill;
            jb->frac = 0.0f;
            jb->skips++;
            jb->fade_left = FADE_FRAMES;  /* soften the discontinuity */
        }
    }

    /* ---- drift control ---- */
    if (jb->drift == VBAN_JB_DRIFT_RESAMPLE) {
        float base = jb->target > 64 ? (float)jb->target : 64.0f;
        float err = (jb->fill_avg - (float)jb->target) / base * DRIFT_GAIN;
        if (err > DRIFT_LIMIT) err = DRIFT_LIMIT;
        if (err < -DRIFT_LIMIT) err = -DRIFT_LIMIT;
        jb->ratio = nominal * (1.0f + err);
    } else {
        jb->ratio = nominal;
    }

    /* ---- adaptive shrink: lowest fill seen over a stable window ---- */
    if ((uint32_t)fill < jb->min_fill) jb->min_fill = (uint32_t)fill;
    jb->stable_frames += frames;
    if (jb->adaptive && jb->stable_frames >= (uint32_t)(rate * STABLE_SECONDS)) {
        uint32_t spare = jb->min_fill / 2;
        if (spare > 16 && jb->target > jb->min_frames) {
            uint32_t t = jb->target > spare ? jb->target - spare : 0;
            jb->target = t < jb->min_frames ? jb->min_frames : t;
        }
        jb->stable_frames = 0;
        jb->min_fill = 0xFFFFFFFFu;
    }

    vban_store_rel(&jb->r, r);
    return frames;
}

size_t vban_jb_pull_f32(vban_jb_t *jb, float *out, size_t frames)
{
    size_t done = 0, got = 0;
    if (!jb || !out) return 0;
    while (done < frames) {   /* chunk so ring math stays in 32 bits */
        uint32_t n = (uint32_t)((frames - done) > 4096 ? 4096 : (frames - done));
        got += pull(jb, out + done * jb->channels, n);
        done += n;
    }
    return got;
}

size_t vban_jb_pull_s16(vban_jb_t *jb, int16_t *out, size_t frames)
{
    float tmp[512];
    size_t done = 0, got = 0;
    uint32_t chunk;
    if (!jb || !out) return 0;
    chunk = 512u / jb->channels;
    if (chunk == 0) return 0; /* > 512 channels: use pull_f32 */
    while (done < frames) {
        uint32_t n = (uint32_t)((frames - done) > chunk ? chunk : (frames - done));
        got += pull(jb, tmp, n);
        vban_pcm_from_f32(tmp, out + done * jb->channels, VBAN_DATATYPE_S16, (size_t)n * jb->channels);
        done += n;
    }
    return got;
}

void vban_jb_request_reset(vban_jb_t *jb)
{
    if (jb) vban_store_rel(&jb->reset_req, 1);
}

void vban_jb_get_stats(const vban_jb_t *jb, vban_jb_stats_t *st)
{
    int32_t fill;
    memset(st, 0, sizeof *st);
    if (!jb) return;
    st->packets = jb->packets;
    st->lost = jb->lost;
    st->late = jb->late;
    st->repaired = jb->repaired;
    st->dup = jb->dup;
    st->resets = jb->resets;
    st->overruns = jb->overruns;
    st->underruns = jb->underruns;
    st->skips = jb->skips;
    st->rejected = jb->rejected;
    st->concealed_frames = jb->concealed;
    fill = (int32_t)(vban_load_acq(&jb->w) - vban_load_acq(&jb->r));
    st->fill_frames = fill > 0 ? (uint32_t)fill : 0;
    st->target_frames = jb->target;
    st->stream_rate = vban_load_rlx(&jb->in_rate);
    st->ratio = jb->ratio;
}

uint32_t vban_jb_latency_us(const vban_jb_t *jb)
{
    int32_t fill = (int32_t)(vban_load_acq(&jb->w) - vban_load_acq(&jb->r));
    float rate = jb->out_rate ? (float)jb->out_rate : stream_rate(jb);
    return fill > 0 ? (uint32_t)((float)fill * 1e6f / rate) : 0;
}
