/* VBAN SDK - transmitters. See vban/vban_tx.h. */
#include "vban/vban_tx.h"
#include "vban/vban_pcm.h"
#include "vban/vban_net.h"
#include <stdint.h>
#include <string.h>

int vban_tx_init(vban_tx_t *tx, const vban_tx_config_t *c)
{
    vban_name_t name;
    unsigned max;
    if (!tx || !c || c->channels == 0 || c->datatype > VBAN_DATATYPE_F64) return VBAN_E_ARG;
    if (vban_sr_to_index(c->sample_rate) < 0) return VBAN_E_ARG;
    max = vban_max_samples(c->channels, c->datatype);
    if (max == 0) return VBAN_E_OVERSIZE;
    memset(tx, 0, sizeof *tx);
    tx->channels = c->channels;
    tx->datatype = c->datatype;
    tx->sample_size = vban_datatype_size[c->datatype];
    tx->sample_rate = c->sample_rate;
    tx->fpp = (uint16_t)(c->frames_per_packet && c->frames_per_packet < max ? c->frames_per_packet : max);
    tx->send = c->send;
    tx->send_ctx = c->send_ctx;
    vban_name_set(&name, c->name);
    vban_build_audio_header(tx->pkt, &name, c->sample_rate, tx->fpp, c->channels, c->datatype, 0);
    return VBAN_OK;
}

int vban_tx_add_dest(vban_tx_t *tx, const vban_addr_t *to)
{
    if (tx->n_dest >= VBAN_TX_MAX_DEST) return VBAN_E_NOSPACE;
    tx->dest[tx->n_dest++] = *to;
    return VBAN_OK;
}

void vban_tx_clear_dest(vban_tx_t *tx) { tx->n_dest = 0; }

void vban_tx_set_name(vban_tx_t *tx, const char *name)
{
    vban_name_t n;
    vban_name_set(&n, name);
    memcpy(tx->pkt + 8, n.b, VBAN_STREAM_NAME_SIZE);
}

static int emit(vban_tx_t *tx)
{
    size_t len;
    unsigned i;
    if (tx->fill == 0) return 0;
    len = VBAN_HEADER_SIZE + (size_t)tx->fill * tx->channels * tx->sample_size;
    vban_header_set_nbs(tx->pkt, (uint8_t)(tx->fill - 1));
    vban_header_set_frame(tx->pkt, tx->frame++);
    for (i = 0; i < tx->n_dest; ++i) {
        if (tx->send && tx->send(tx->send_ctx, tx->pkt, len, &tx->dest[i]) >= 0) tx->stats.bytes += len;
        else tx->stats.errors++;
    }
    tx->stats.packets++;
    tx->fill = 0;

    if (tx->red_delay) {
        /* history slot `head` holds the packet sent red_delay emissions ago:
         * send that copy now, then store the current packet in its place */
        uint8_t *slot = tx->red_buf + (size_t)tx->red_head * VBAN_PACKET_MAX_SIZE;
        if (tx->red_count >= tx->red_delay) {
            size_t rl = tx->red_len[tx->red_head];
            for (i = 0; i < tx->n_dest; ++i) {
                if (tx->send && tx->send(tx->send_ctx, slot, rl, &tx->dest[i]) >= 0) tx->stats.bytes += rl;
                else tx->stats.errors++;
            }
            tx->stats.redundant++;
        } else {
            tx->red_count++;
        }
        memcpy(slot, tx->pkt, len);
        tx->red_len[tx->red_head] = (uint16_t)len;
        tx->red_head = (uint8_t)((tx->red_head + 1) % tx->red_delay);
    }
    return 1;
}

int vban_tx_set_redundancy(vban_tx_t *tx, unsigned delay, void *buf, size_t size)
{
    if (!tx || delay > VBAN_TX_REDUNDANCY_MAX) return VBAN_E_ARG;
    if (delay && (!buf || size < (size_t)delay * VBAN_PACKET_MAX_SIZE)) return VBAN_E_NOSPACE;
    tx->red_buf = delay ? (uint8_t *)buf : NULL;
    tx->red_delay = (uint8_t)delay;
    tx->red_head = 0;
    tx->red_count = 0;
    return VBAN_OK;
}

typedef int (*conv_fn)(const void *src, size_t src_off_samples, uint8_t *dst, unsigned dt, size_t n);

static int conv_f32(const void *s, size_t off, uint8_t *d, unsigned dt, size_t n)
{ return vban_pcm_from_f32((const float *)s + off, d, dt, n); }
static int conv_s16(const void *s, size_t off, uint8_t *d, unsigned dt, size_t n)
{ return vban_pcm_from_s16((const int16_t *)s + off, d, dt, n); }

static int write_generic(vban_tx_t *tx, const void *in, size_t frames, conv_fn conv, size_t raw_ss)
{
    int sent = 0, r;
    size_t done = 0, ch, fb;
    if (!tx || (!in && frames)) return VBAN_E_ARG;
    ch = tx->channels;
    fb = ch * tx->sample_size;
    while (done < frames) {
        size_t n = tx->fpp - tx->fill;
        uint8_t *dst = tx->pkt + VBAN_HEADER_SIZE + (size_t)tx->fill * fb;
        if (n > frames - done) n = frames - done;
        if (conv) r = conv(in, done * ch, dst, tx->datatype, n * ch);
        else { memcpy(dst, (const uint8_t *)in + done * ch * raw_ss, n * fb); r = VBAN_OK; }
        if (r != VBAN_OK) return r;
        tx->fill = (uint16_t)(tx->fill + n);
        done += n;
        if (tx->fill == tx->fpp) sent += emit(tx);
    }
    return sent;
}

int vban_tx_write_f32(vban_tx_t *tx, const float *in, size_t frames) { return write_generic(tx, in, frames, conv_f32, 0); }
int vban_tx_write_s16(vban_tx_t *tx, const int16_t *in, size_t frames) { return write_generic(tx, in, frames, conv_s16, 0); }
int vban_tx_write_raw(vban_tx_t *tx, const void *in, size_t frames) { return write_generic(tx, in, frames, NULL, tx->sample_size); }

int vban_tx_write_f32_planar(vban_tx_t *tx, const float *const *in, size_t frames)
{
    const float *ptr[64];
    int sent = 0, r;
    size_t done = 0;
    unsigned c;
    if (!tx || !in) return VBAN_E_ARG;
    if (tx->channels > 64) return VBAN_E_UNSUPPORTED;
    while (done < frames) {
        size_t n = tx->fpp - tx->fill;
        if (n > frames - done) n = frames - done;
        for (c = 0; c < tx->channels; ++c) ptr[c] = in[c] ? in[c] + done : NULL;
        r = vban_pcm_from_f32_planar(ptr, tx->channels, n,
                                     tx->pkt + VBAN_HEADER_SIZE + (size_t)tx->fill * tx->channels * tx->sample_size,
                                     tx->datatype);
        if (r != VBAN_OK) return r;
        tx->fill = (uint16_t)(tx->fill + n);
        done += n;
        if (tx->fill == tx->fpp) sent += emit(tx);
    }
    return sent;
}

int vban_tx_flush(vban_tx_t *tx) { return emit(tx); }

/* ------------------------------------------------------------ pacer */

void vban_pacer_init(vban_pacer_t *p, uint32_t rate)
{
    p->start_ns = vban_time_ns();
    p->frames = 0;
    p->rate = rate;
}

void vban_pacer_wait(vban_pacer_t *p, uint32_t frames)
{
    uint64_t due;
    p->frames += frames;
    due = p->start_ns + p->frames * 1000000000ull / p->rate;
    vban_sleep_until_ns(due);
}

/* ------------------------------------------------------------ data streams */

void vban_out_init(vban_out_t *o, const char *name, const vban_addr_t *dest, vban_send_fn send, void *ctx)
{
    memset(o, 0, sizeof *o);
    vban_name_set(&o->name, name);
    if (dest) o->dest = *dest;
    o->send = send;
    o->send_ctx = ctx;
}

static int out_send(vban_out_t *o, const uint8_t *pkt, int len)
{
    if (len < 0) return len;
    if (!o->send || o->send(o->send_ctx, pkt, (size_t)len, &o->dest) < 0) return VBAN_E_NET;
    return len;
}

int vban_send_text_ex(vban_out_t *o, unsigned bps, unsigned ch, unsigned fmt, const void *data, size_t len)
{
    uint8_t pkt[VBAN_PACKET_MAX_SIZE];
    int n = vban_build_text(pkt, sizeof pkt, &o->name, o->frame, bps, ch, fmt, data, len);
    if (n >= 0) o->frame++;
    return out_send(o, pkt, n);
}

int vban_send_text(vban_out_t *o, const char *s, int len)
{
    if (!s) return VBAN_E_ARG;
    return vban_send_text_ex(o, 18, 0, VBAN_TXT_UTF8, s, len < 0 ? strlen(s) : (size_t)len);
}

int vban_send_serial(vban_out_t *o, unsigned bps, unsigned ch, unsigned bitmode, unsigned type,
                     const void *data, size_t len)
{
    uint8_t pkt[VBAN_PACKET_MAX_SIZE];
    const uint8_t *d = (const uint8_t *)data;
    int count = 0;
    do {
        size_t n = len > VBAN_DATA_MAX_SIZE ? VBAN_DATA_MAX_SIZE : len;
        unsigned mode = (bitmode & 0x7F) | (len > n ? VBAN_SERIAL_MULTIPART : 0);
        int r = vban_build_serial(pkt, sizeof pkt, &o->name, o->frame, bps, ch, mode, type, d, n);
        if (r < 0) return r;
        o->frame++;
        if ((r = out_send(o, pkt, r)) < 0) return r;
        d += n;
        len -= n;
        ++count;
    } while (len);
    return count;
}

int vban_send_midi(vban_out_t *o, const void *data, size_t len)
{
    /* 31250 bps (index 11), 1 start + 1 stop bit, no parity */
    return vban_send_serial(o, 11, 0, VBAN_SERIAL_START | VBAN_SERIAL_STOP_1, VBAN_SERIAL_MIDI, data, len);
}

int vban_send_frame(vban_out_t *o, unsigned mbps, unsigned stype, const void *data, size_t len)
{
    uint8_t pkt[VBAN_PACKET_MAX_SIZE];
    const uint8_t *d = (const uint8_t *)data;
    size_t total = len ? (len + VBAN_DATA_MAX_SIZE - 1) / VBAN_DATA_MAX_SIZE : 1, i;
#if SIZE_MAX > 0xFFFFu /* a 16-bit size_t (AVR) cannot exceed it; avoids -Wtype-limits */
    if (total > 65536) return VBAN_E_OVERSIZE;
#endif
    for (i = 0; i < total; ++i) {
        size_t n = len > VBAN_DATA_MAX_SIZE ? VBAN_DATA_MAX_SIZE : len;
        unsigned type = (i == 0 ? VBAN_FRAME_START : 0) | (i + 1 == total ? VBAN_FRAME_END : 0);
        int r;
        if (!type) type = VBAN_FRAME_CONTINUE;
        r = vban_build_frame_packet(pkt, sizeof pkt, &o->name, o->frame, mbps, (uint16_t)i, type, stype, d, n);
        if ((r = out_send(o, pkt, r)) < 0) return r;
        d += n;
        len -= n;
    }
    o->frame++; /* one counter value per frame */
    return (int)total;
}

int vban_send_ping(vban_out_t *o, const vban_ping0_t *self)
{
    uint8_t pkt[VBAN_PING0_PACKET_SIZE];
    uint32_t id = o->frame++;
    int n = vban_build_ping(pkt, sizeof pkt, id, 0, o->name.b[0] ? &o->name : NULL, self);
    n = out_send(o, pkt, n);
    return n < 0 ? n : (int)(id & 0x7FFFFFFF);
}
