/* VBAN SDK - wire protocol implementation. See vban/vban_proto.h. */
#include "vban/vban_proto.h"
#include <string.h>

const uint32_t vban_sr_table[VBAN_SR_COUNT] = {
    6000, 12000, 24000, 48000, 96000, 192000, 384000,
    8000, 16000, 32000, 64000, 128000, 256000, 512000,
    11025, 22050, 44100, 88200, 176400, 352800, 705600
};

const uint32_t vban_bps_table[VBAN_BPS_COUNT] = {
    0, 110, 150, 300, 600, 1200, 2400, 4800, 9600, 14400,
    19200, 31250, 38400, 57600, 115200, 128000, 230400, 250000, 256000, 460800,
    921600, 1000000, 1500000, 2000000, 3000000
};

const uint16_t vban_mbps_table[VBAN_MBPS_COUNT] = {
    0, 1, 2, 3, 4, 5, 6, 8, 10, 12, 16, 24, 36, 48, 60, 84, 108, 156, 204, 252, 300, 400,
    500, 600, 800
};

const uint8_t vban_datatype_size[8] = { 1, 2, 3, 4, 4, 8, 0, 0 };

const char *vban_strerror(int err)
{
    switch (err) {
    case VBAN_OK:            return "ok";
    case VBAN_E_ARG:         return "invalid argument";
    case VBAN_E_SHORT:       return "packet shorter than VBAN header";
    case VBAN_E_MAGIC:       return "not a VBAN packet";
    case VBAN_E_SIZE:        return "payload size does not match header";
    case VBAN_E_FORMAT:      return "invalid or reserved header field";
    case VBAN_E_UNSUPPORTED: return "format not supported";
    case VBAN_E_NOSPACE:     return "no space left";
    case VBAN_E_NET:         return "network error";
    case VBAN_E_TIMEOUT:     return "timeout";
    case VBAN_E_OVERSIZE:    return "packet exceeds VBAN limits";
    case VBAN_E_NOTFOUND:    return "not found";
    case VBAN_E_AUTH:        return "authentication failed";
    case VBAN_E_CERT:        return "certificate rejected";
    case VBAN_E_REPLAY:      return "replayed or too old";
    case VBAN_E_STATE:       return "unexpected in this session state";
    default:                 return "unknown error";
    }
}

uint32_t vban_sr_from_index(unsigned index)
{
    return index < VBAN_SR_COUNT ? vban_sr_table[index] : 0;
}

int vban_sr_to_index(uint32_t rate)
{
    int i;
    for (i = 0; i < VBAN_SR_COUNT; ++i)
        if (vban_sr_table[i] == rate) return i;
    return -1;
}

uint32_t vban_bps_from_index(unsigned index)
{
    return index < VBAN_BPS_COUNT ? vban_bps_table[index] : 0;
}

int vban_bps_to_index(uint32_t bps)
{
    int i;
    for (i = 0; i < VBAN_BPS_COUNT; ++i)
        if (vban_bps_table[i] == bps) return i;
    return -1;
}

/* ---------------------------------------------------------------- names */

void vban_name_set(vban_name_t *n, const char *s)
{
    size_t i = 0;
    if (s)
        for (; i < VBAN_STREAM_NAME_SIZE && s[i]; ++i) n->b[i] = (uint8_t)s[i];
    for (; i < VBAN_STREAM_NAME_SIZE; ++i) n->b[i] = 0;
}

void vban_name_get(const vban_name_t *n, char out[VBAN_STREAM_NAME_SIZE + 1])
{
    size_t i;
    for (i = 0; i < VBAN_STREAM_NAME_SIZE && n->b[i]; ++i) out[i] = (char)n->b[i];
    out[i] = 0;
}

int vban_name_eq(const vban_name_t *a, const uint8_t wire[VBAN_STREAM_NAME_SIZE])
{
    /* Fast path: senders normally zero-pad, so a straight 16-byte compare
     * settles it. Otherwise compare up to the first NUL (garbage after the
     * terminator is ignored, as several implementations leave it there). */
    size_t i;
    if (memcmp(a->b, wire, VBAN_STREAM_NAME_SIZE) == 0) return 1;
    for (i = 0; i < VBAN_STREAM_NAME_SIZE; ++i) {
        if (a->b[i] != wire[i]) return 0;
        if (a->b[i] == 0) return 1;
    }
    return 1;
}

static uint8_t lower_ascii(uint8_t c) { return (c >= 'A' && c <= 'Z') ? (uint8_t)(c + 32) : c; }

int vban_name_eq_nocase(const vban_name_t *a, const uint8_t wire[VBAN_STREAM_NAME_SIZE])
{
    size_t i;
    for (i = 0; i < VBAN_STREAM_NAME_SIZE; ++i) {
        if (lower_ascii(a->b[i]) != lower_ascii(wire[i])) return 0;
        if (a->b[i] == 0) return 1;
    }
    return 1;
}

/* --------------------------------------------------------------- header */

void vban_header_write(uint8_t dst[VBAN_HEADER_SIZE], const vban_header_t *h)
{
    dst[0] = 'V'; dst[1] = 'B'; dst[2] = 'A'; dst[3] = 'N';
    dst[4] = h->format_SR;
    dst[5] = h->format_nbs;
    dst[6] = h->format_nbc;
    dst[7] = h->format_bit;
    memcpy(dst + 8, h->name.b, VBAN_STREAM_NAME_SIZE);
    vban_wr32(dst + 24, h->nu_frame);
}

void vban_header_read(const uint8_t src[VBAN_HEADER_SIZE], vban_header_t *h)
{
    h->format_SR  = src[4];
    h->format_nbs = src[5];
    h->format_nbc = src[6];
    h->format_bit = src[7];
    memcpy(h->name.b, src + 8, VBAN_STREAM_NAME_SIZE);
    h->nu_frame = vban_rd32(src + 24);
}

unsigned vban_max_samples(unsigned channels, unsigned datatype)
{
    unsigned frame_bytes = channels * vban_datatype_size[datatype & 7];
    unsigned n;
    if (frame_bytes == 0) return 0;
    n = VBAN_DATA_MAX_SIZE / frame_bytes;
    return n > VBAN_SAMPLES_MAX ? VBAN_SAMPLES_MAX : n;
}

/* ---------------------------------------------------------------- parse */

static int parse_body(const uint8_t *buf, size_t len, vban_packet_t *o, unsigned flags);

int vban_parse(const uint8_t *buf, size_t len, vban_packet_t *o, unsigned flags)
{
    int r;
    if (!buf || !o) return VBAN_E_ARG;
    o->valid = 0;
    r = parse_body(buf, len, o, flags);
    o->valid = (uint8_t)(r == VBAN_OK);
    return r;
}

static int parse_body(const uint8_t *buf, size_t len, vban_packet_t *o, unsigned flags)
{
    uint8_t sr;
    if (len < VBAN_HEADER_SIZE) return VBAN_E_SHORT;
    if (buf[0] != 'V' || buf[1] != 'B' || buf[2] != 'A' || buf[3] != 'N') return VBAN_E_MAGIC;
    if (len > VBAN_PACKET_MAX_SIZE && !(flags & VBAN_PARSE_ALLOW_OVERSIZE)) return VBAN_E_OVERSIZE;
    if (len > 0xFFFF) return VBAN_E_OVERSIZE;

    sr = buf[4];
    o->raw        = buf;
    o->name       = buf + 8;
    o->data       = buf + VBAN_HEADER_SIZE;
    o->data_len   = (uint16_t)(len - VBAN_HEADER_SIZE);
    o->protocol   = sr & VBAN_PROTOCOL_MASK;
    o->sr_index   = sr & VBAN_SR_MASK;
    o->format_nbs = buf[5];
    o->format_nbc = buf[6];
    o->format_bit = buf[7];
    o->nu_frame   = vban_rd32(buf + 24);
    o->samples    = (uint16_t)(o->format_nbs + 1);
    o->channels   = (uint16_t)(o->format_nbc + 1);
    o->datatype   = o->format_bit & VBAN_DATATYPE_MASK;
    o->codec      = o->format_bit & VBAN_CODEC_MASK;
    o->sample_size = vban_datatype_size[o->datatype];
    o->sample_rate = vban_sr_from_index(o->sr_index);

    switch (o->protocol) {
    case VBAN_PROTOCOL_AUDIO:
        if (o->format_bit & VBAN_RESERVED_MASK) return VBAN_E_FORMAT;
        if (o->sample_rate == 0) return VBAN_E_FORMAT;
        if (o->codec != VBAN_CODEC_PCM) return VBAN_E_UNSUPPORTED;
        if (o->sample_size == 0) return VBAN_E_UNSUPPORTED;
        if ((size_t)o->samples * o->channels * o->sample_size != o->data_len) return VBAN_E_SIZE;
        return VBAN_OK;
    case VBAN_PROTOCOL_SERIAL:
    case VBAN_PROTOCOL_TXT:
        if (flags & VBAN_PARSE_STRICT) {
            if (o->format_bit & (VBAN_RESERVED_MASK | VBAN_DATATYPE_MASK)) return VBAN_E_FORMAT;
            if (o->sr_index >= VBAN_BPS_COUNT) return VBAN_E_FORMAT;
        }
        return VBAN_OK;
    case VBAN_PROTOCOL_FRAME:
        if (flags & VBAN_PARSE_STRICT) {
            unsigned t = o->format_bit & 0x07;
            if (o->format_bit & VBAN_RESERVED_MASK) return VBAN_E_FORMAT;
            if (t == 0 || o->sr_index >= VBAN_MBPS_COUNT) return VBAN_E_FORMAT;
        }
        return VBAN_OK;
    case VBAN_PROTOCOL_SERVICE:
        if ((flags & VBAN_PARSE_STRICT) && o->sr_index != 0) return VBAN_E_FORMAT;
        return VBAN_OK;
    default:
        return VBAN_OK; /* undefined / user protocols: header only */
    }
}

/* ------------------------------------------------------------ sequencing */

void vban_seq_init(vban_seq_t *s, uint32_t max_gap)
{
    memset(s, 0, sizeof *s);
    s->max_gap = max_gap ? max_gap : 64;
}

vban_seq_result_t vban_seq_update(vban_seq_t *s, uint32_t f, uint32_t *lost)
{
    int32_t d;
    if (lost) *lost = 0;
    if (!s->started) {
        s->started = 1;
        s->expected = f + 1;
        s->has_cand = s->has_prev = 0;
        return VBAN_SEQ_FIRST;
    }
    d = (int32_t)(f - s->expected); /* wrap-safe */
    if (d == 0) {
        s->expected = f + 1;
        s->has_cand = 0;
        s->n_hist = 0;
        if (s->has_prev && ++s->since_jump > 32) s->has_prev = 0;
        return VBAN_SEQ_OK;
    }
    /* After a restart-size jump, the sequence we left keeps going (two
     * consecutive packets): that jump was injected, go back to the real stream.
     * One stray packet (a late copy) is not enough. */
    if (s->has_prev && (uint32_t)(f - s->prev) <= 3) {
        if (s->prev_hit && f == s->prev_last + 1) {
            s->expected = f + 1;
            s->has_prev = s->has_cand = s->prev_hit = 0;
            return VBAN_SEQ_RESET;
        }
        s->prev_hit = 1;
        s->prev_last = f;
        return VBAN_SEQ_LATE;
    }
    if (d > 0 && (uint32_t)d <= VBAN_SEQ_CONFIRM_GAP) {
        s->expected = f + 1;
        s->has_cand = 0;
        if (lost) *lost = (uint32_t)d;
        return VBAN_SEQ_GAP;
    }
    if (d < 0 && (uint32_t)(-d) <= s->max_gap) return VBAN_SEQ_LATE;

    /* Big jump forward or backward: accept once a second packet lands near it.
     * "Near" (not just counter + 1) so reordered packets and redundant copies
     * confirm the jump instead of displacing the candidate forever. */
    if (s->has_cand) {
        unsigned h;
        for (h = 0; h < s->n_hist; ++h) {
            int32_t dc = (int32_t)(f - s->hist[h]);
            if (dc != 0 && (uint32_t)(dc < 0 ? -dc : dc) <= VBAN_SEQ_CONFIRM_GAP) break;
        }
        if (h < s->n_hist) {
            int32_t dc = (int32_t)(f - s->hist[h]);
            uint32_t top = dc > 0 ? f : s->hist[h];
            int32_t dt = (int32_t)(top - s->expected);
            s->has_cand = 0;
            s->n_hist = 0;
            if (dt > 0 && (uint32_t)dt <= s->max_gap) {   /* long loss: stays a gap */
                s->expected = top + 1;
                if (lost) *lost = (uint32_t)dt;
                return dc > 0 ? VBAN_SEQ_GAP : VBAN_SEQ_LATE;
            }
            s->prev = s->expected;                         /* restart */
            s->has_prev = 1;
            s->prev_hit = 0;
            s->since_jump = 0;
            s->expected = top + 1;
            return VBAN_SEQ_RESET;
        }
    }
    if (!s->has_cand) s->n_hist = 0;
    s->cand = f;
    s->has_cand = 1;
    if (s->n_hist < 4) s->hist[s->n_hist++] = f;
    else { s->hist[0] = s->hist[1]; s->hist[1] = s->hist[2]; s->hist[2] = s->hist[3]; s->hist[3] = f; }
    return VBAN_SEQ_SUSPECT;
}

/* ------------------------------------------------------------- builders */

static void put_header(uint8_t *dst, uint8_t sr, uint8_t nbs, uint8_t nbc, uint8_t bit,
                       const vban_name_t *name, uint32_t f)
{
    dst[0] = 'V'; dst[1] = 'B'; dst[2] = 'A'; dst[3] = 'N';
    dst[4] = sr; dst[5] = nbs; dst[6] = nbc; dst[7] = bit;
    if (name) memcpy(dst + 8, name->b, VBAN_STREAM_NAME_SIZE);
    else memset(dst + 8, 0, VBAN_STREAM_NAME_SIZE);
    vban_wr32(dst + 24, f);
}

int vban_build_audio_header(uint8_t dst[VBAN_HEADER_SIZE], const vban_name_t *name,
                            uint32_t sample_rate, unsigned samples, unsigned channels,
                            unsigned datatype, uint32_t nu_frame)
{
    int idx = vban_sr_to_index(sample_rate);
    if (!dst || idx < 0 || samples < 1 || samples > VBAN_SAMPLES_MAX ||
        channels < 1 || channels > VBAN_CHANNELS_MAX || datatype > 7)
        return VBAN_E_ARG;
    put_header(dst, (uint8_t)(idx | VBAN_PROTOCOL_AUDIO), (uint8_t)(samples - 1),
               (uint8_t)(channels - 1), (uint8_t)(datatype | VBAN_CODEC_PCM), name, nu_frame);
    return VBAN_HEADER_SIZE;
}

static int build_data(uint8_t *dst, size_t cap, uint8_t sr, uint8_t nbs, uint8_t nbc, uint8_t bit,
                      const vban_name_t *name, uint32_t f, const void *data, size_t len)
{
    if (!dst || (len && !data)) return VBAN_E_ARG;
    if (len > VBAN_DATA_MAX_SIZE) return VBAN_E_OVERSIZE;
    if (cap < VBAN_HEADER_SIZE + len) return VBAN_E_NOSPACE;
    put_header(dst, sr, nbs, nbc, bit, name, f);
    if (len) memcpy(dst + VBAN_HEADER_SIZE, data, len);
    return (int)(VBAN_HEADER_SIZE + len);
}

int vban_build_text(uint8_t *dst, size_t cap, const vban_name_t *name, uint32_t nu_frame,
                    unsigned bps_index, unsigned channel, unsigned txt_format,
                    const void *text, size_t len)
{
    if (bps_index >= VBAN_BPS_COUNT || channel > 255) return VBAN_E_ARG;
    return build_data(dst, cap, (uint8_t)(VBAN_PROTOCOL_TXT | bps_index), 0, (uint8_t)channel,
                      (uint8_t)(txt_format & 0xF0), name, nu_frame, text, len);
}

int vban_build_serial(uint8_t *dst, size_t cap, const vban_name_t *name, uint32_t nu_frame,
                      unsigned bps_index, unsigned channel, unsigned bitmode, unsigned type,
                      const void *data, size_t len)
{
    if (bps_index >= VBAN_BPS_COUNT || channel > 255 || bitmode > 255) return VBAN_E_ARG;
    return build_data(dst, cap, (uint8_t)(VBAN_PROTOCOL_SERIAL | bps_index), (uint8_t)bitmode,
                      (uint8_t)channel, (uint8_t)(type & 0xF0), name, nu_frame, data, len);
}

int vban_build_frame_packet(uint8_t *dst, size_t cap, const vban_name_t *name, uint32_t nu_frame,
                            unsigned mbps_index, uint16_t packet_index, unsigned packet_type,
                            unsigned stream_type, const void *data, size_t len)
{
    if (mbps_index >= VBAN_MBPS_COUNT || (packet_type & ~7u) || packet_type == 0) return VBAN_E_ARG;
    return build_data(dst, cap, (uint8_t)(VBAN_PROTOCOL_FRAME | mbps_index),
                      (uint8_t)(packet_index & 0xFF), (uint8_t)(packet_index >> 8),
                      (uint8_t)((stream_type & 0xF0) | packet_type), name, nu_frame, data, len);
}

/* ------------------------------------------------------------- PING0 */

/* Offsets inside the 676-byte tagVBAN_PING0 structure. */
enum {
    P0_TYPE = 0, P0_FEAT = 4, P0_FEATEX = 8, P0_PREF = 12, P0_MIN = 16, P0_MAX = 20,
    P0_COLOR = 24, P0_VERSION = 28, P0_GPS = 32, P0_USERPOS = 40, P0_LANG = 48,
    P0_RES_ASCII = 56, P0_RESEX = 64, P0_DIP = 128, P0_DPORT = 160, P0_DRES = 162,
    P0_DEVICE = 164, P0_MANUF = 228, P0_APP = 292, P0_HOST = 356, P0_USER = 420,
    P0_COMMENT = 548, P0_END = 676
};

static void put_str(uint8_t *dst, const char *s, size_t n)
{
    size_t i = 0;
    for (; i < n && s[i]; ++i) dst[i] = (uint8_t)s[i];
    for (; i < n; ++i) dst[i] = 0;
}

static void get_str(char *dst, size_t dst_n, const uint8_t *src, size_t src_n, size_t avail)
{
    size_t i, n = src_n < avail ? src_n : avail;
    if (n > dst_n - 1) n = dst_n - 1;
    for (i = 0; i < n && src[i]; ++i) dst[i] = (char)src[i];
    dst[i] = 0;
}

void vban_ping0_encode(const vban_ping0_t *p, uint8_t o[VBAN_PING0_SIZE])
{
    memset(o, 0, VBAN_PING0_SIZE);
    vban_wr32(o + P0_TYPE, p->bit_type);
    vban_wr32(o + P0_FEAT, p->bit_feature);
    vban_wr32(o + P0_FEATEX, p->bit_feature_ex);
    vban_wr32(o + P0_PREF, p->prefered_rate);
    vban_wr32(o + P0_MIN, p->min_rate);
    vban_wr32(o + P0_MAX, p->max_rate);
    vban_wr32(o + P0_COLOR, p->color_rgb);
    memcpy(o + P0_VERSION, p->version, 4);
    memcpy(o + P0_GPS, p->gps_position, 8);        /* binary tagVBAN_GPS_STRUCT */
    memcpy(o + P0_USERPOS, p->user_position, 8);
    put_str(o + P0_LANG, p->lang_code, 8);
    put_str(o + P0_DIP, p->distant_ip, 32);
    vban_wr16(o + P0_DPORT, p->distant_port);
    put_str(o + P0_DEVICE, p->device_name, 64);
    put_str(o + P0_MANUF, p->manufacturer_name, 64);
    put_str(o + P0_APP, p->application_name, 64);
    put_str(o + P0_HOST, p->host_name, 64);
    put_str(o + P0_USER, p->user_name, 128);
    put_str(o + P0_COMMENT, p->user_comment, 128);
}

static uint32_t rd32_avail(const uint8_t *in, size_t len, size_t off)
{
    return off + 4 <= len ? vban_rd32(in + off) : 0;
}

#define GETS(field, off, n) \
    get_str(out->field, sizeof(out->field), in + (off), (n), len > (off) ? len - (off) : 0)

int vban_ping0_decode(const uint8_t *in, size_t len, vban_ping0_t *out)
{
    if (!in || !out) return VBAN_E_ARG;
    memset(out, 0, sizeof(*out));
    out->bit_type       = rd32_avail(in, len, P0_TYPE);
    out->bit_feature    = rd32_avail(in, len, P0_FEAT);
    out->bit_feature_ex = rd32_avail(in, len, P0_FEATEX);
    out->prefered_rate  = rd32_avail(in, len, P0_PREF);
    out->min_rate       = rd32_avail(in, len, P0_MIN);
    out->max_rate       = rd32_avail(in, len, P0_MAX);
    out->color_rgb      = rd32_avail(in, len, P0_COLOR);
    if (len >= P0_VERSION + 4) memcpy(out->version, in + P0_VERSION, 4);
    /* GPS_Position is 8 raw bytes (tagVBAN_GPS_STRUCT), keep them verbatim */
    if (len >= P0_GPS + 8) memcpy(out->gps_position, in + P0_GPS, 8);
    if (len >= P0_USERPOS + 8) memcpy(out->user_position, in + P0_USERPOS, 8);
    GETS(lang_code, P0_LANG, 8);
    GETS(distant_ip, P0_DIP, 32);
    if (len >= P0_DPORT + 2) out->distant_port = vban_rd16(in + P0_DPORT);
    GETS(device_name, P0_DEVICE, 64);
    GETS(manufacturer_name, P0_MANUF, 64);
    GETS(application_name, P0_APP, 64);
    GETS(host_name, P0_HOST, 64);
    GETS(user_name, P0_USER, 128);
    GETS(user_comment, P0_COMMENT, 128);
    return len >= P0_END ? VBAN_OK : VBAN_E_SHORT;
}

int vban_build_ping(uint8_t *dst, size_t cap, uint32_t request_id, int reply,
                    const vban_name_t *label, const vban_ping0_t *ping)
{
    vban_name_t def;
    size_t need = VBAN_HEADER_SIZE + (ping ? VBAN_PING0_SIZE : 0);
    if (!dst) return VBAN_E_ARG;
    if (cap < need) return VBAN_E_NOSPACE;
    if (!label) { vban_name_set(&def, "VBAN SPOT PING"); label = &def; }
    put_header(dst, VBAN_PROTOCOL_SERVICE,
               (uint8_t)(VBAN_SERVICE_FNCT_PING0 | (reply ? VBAN_SERVICE_FNCT_REPLY : 0)),
               VBAN_SERVICE_IDENTIFICATION, 0, label, request_id);
    if (ping) vban_ping0_encode(ping, dst + VBAN_HEADER_SIZE);
    return (int)need;
}

int vban_build_rt_register(uint8_t *dst, size_t cap, const vban_name_t *label, uint32_t nu_frame,
                           unsigned packet_id, uint8_t timeout_s, const uint8_t *ids, size_t n_ids)
{
    size_t i;
    if (packet_id > 127 || n_ids > 128) return VBAN_E_ARG;
    for (i = 0; i < n_ids; ++i) if (ids[i] > 127) return VBAN_E_ARG;
    return build_data(dst, cap, VBAN_PROTOCOL_SERVICE, (uint8_t)packet_id, VBAN_SERVICE_RTPACKETREGISTER,
                      timeout_s, label, nu_frame, ids, n_ids);
}

int vban_build_rt_register_reply(uint8_t *dst, size_t cap, const vban_name_t *label,
                                 uint32_t nu_frame, unsigned packet_id, unsigned code)
{
    if (packet_id > 127 || code > 255) return VBAN_E_ARG;
    return build_data(dst, cap, VBAN_PROTOCOL_SERVICE, (uint8_t)(packet_id | VBAN_SERVICE_FNCT_REPLY),
                      VBAN_SERVICE_RTPACKETREGISTER, (uint8_t)code, label, nu_frame, NULL, 0);
}

int vban_build_rt_packet(uint8_t *dst, size_t cap, const vban_name_t *label, uint32_t nu_frame,
                         unsigned packet_id, const void *data, size_t len)
{
    if (packet_id > 127) return VBAN_E_ARG;
    return build_data(dst, cap, VBAN_PROTOCOL_SERVICE, (uint8_t)packet_id, VBAN_SERVICE_RTPACKET,
                      0, label, nu_frame, data, len);
}
