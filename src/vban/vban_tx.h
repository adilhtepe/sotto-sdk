/*
 * VBAN SDK - transmitters.
 *
 * vban_tx_t is an audio packetizer: write any number of frames in any
 * supported sample format; samples are converted straight into the outgoing
 * packet buffer (no intermediate copy) and a packet is emitted to every
 * destination as soon as `frames_per_packet` frames are collected.
 *
 * Latency: a packet leaves after frames_per_packet / rate seconds of audio
 * (256 @ 48 kHz = 5.3 ms, 64 = 1.3 ms, 32 = 0.67 ms). Smaller packets mean
 * lower latency and more packets per second; the receiver must keep up.
 * Set frames_per_packet to your audio period (if <= 256) so each callback
 * goes out immediately.
 *
 * Also: data-stream senders (TEXT, SERIAL/MIDI with multipart, FRAME,
 * PING0) and a wall-clock pacer for sources without an audio clock.
 */
#ifndef VBAN_TX_H
#define VBAN_TX_H

#include <stddef.h>
#include <stdint.h>
#include "vban_proto.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct vban_tx_config {
    const char  *name;               /* stream name (<= 16 chars) */
    uint32_t     sample_rate;        /* must be in vban_sr_table */
    uint16_t     channels;           /* 1..256 */
    uint8_t      datatype;           /* VBAN_DATATYPE_* (wire format) */
    uint16_t     frames_per_packet;  /* 1..256; 0 = largest that fits 1436 bytes */
    vban_send_fn send;
    void        *send_ctx;
} vban_tx_config_t;

typedef struct vban_tx_stats { uint32_t packets, redundant, errors; uint64_t bytes; } vban_tx_stats_t;

typedef struct vban_tx {
    uint8_t      pkt[VBAN_PACKET_MAX_SIZE];
    vban_addr_t  dest[VBAN_TX_MAX_DEST];
    uint8_t      n_dest;
    uint8_t      datatype, sample_size;
    uint16_t     channels, fpp, fill;
    uint32_t     sample_rate;
    uint32_t     frame;
    vban_send_fn send;
    void        *send_ctx;
    vban_tx_stats_t stats;
    /* redundancy (time-diversity copies), see vban_tx_set_redundancy() */
    uint8_t     *red_buf;
    uint16_t     red_len[VBAN_TX_REDUNDANCY_MAX];
    uint8_t      red_delay, red_head, red_count;
} vban_tx_t;

int  vban_tx_init(vban_tx_t *tx, const vban_tx_config_t *cfg);
int  vban_tx_add_dest(vban_tx_t *tx, const vban_addr_t *to);
void vban_tx_clear_dest(vban_tx_t *tx);
void vban_tx_set_name(vban_tx_t *tx, const char *name);

/* Write interleaved frames; return packets emitted or negative error. */
int  vban_tx_write_f32(vban_tx_t *tx, const float *in, size_t frames);
int  vban_tx_write_s16(vban_tx_t *tx, const int16_t *in, size_t frames);
/* Samples already in the stream's wire format (little-endian, interleaved). */
int  vban_tx_write_raw(vban_tx_t *tx, const void *in, size_t frames);
/* Planar float channels (in[c] may be NULL for silence). */
int  vban_tx_write_f32_planar(vban_tx_t *tx, const float *const *in, size_t frames);
/* Emit the partially filled packet now (fewer samples, legal in VBAN). */
int  vban_tx_flush(vban_tx_t *tx);

/*
 * Loss protection by time diversity: every packet is sent again, unchanged
 * (same frame counter), `delay_packets` packets later, riding along with a
 * newer packet. A Wi-Fi / WAN loss burst shorter than the delay is repaired
 * by the copy, which the SDK jitter buffer drops in its slot before playout.
 * Costs 2x bandwidth and delay_packets x 1464 bytes of caller memory
 * (`buf`), no CPU worth mentioning and no added latency as long as the delay
 * is shorter than the receiver's buffering.
 *
 * Measured with vban_netsim (64-frame packets, 120 s): delay 4 (~5 ms) cut
 * audible loss by 91 % on "Wi-Fi good" (292 -> 27 ms/min) and 79 % on
 * "Wi-Fi congested" (1799 -> 370 ms/min) at unchanged latency; longer delays
 * repair less because copies arrive after playout. Aim for ~5 ms:
 * delay = round(5 ms * rate / frames_per_packet), 1..VBAN_TX_REDUNDANCY_MAX.
 *
 * Receivers that drop duplicate counters see a plain stream. Voicemeeter's
 * handling of duplicates is not verified: enable it for SDK receivers.
 * delay_packets = 0 disables. Returns VBAN_E_NOSPACE if buf is too small.
 */
int  vban_tx_set_redundancy(vban_tx_t *tx, unsigned delay_packets, void *buf, size_t buf_size);

/* -------------------------------------------------- pacer */
/* For file / generator sources: blocks until `frames` more frames are due. */
typedef struct vban_pacer { uint64_t start_ns; uint64_t frames; uint32_t rate; } vban_pacer_t;
void vban_pacer_init(vban_pacer_t *p, uint32_t sample_rate);
void vban_pacer_wait(vban_pacer_t *p, uint32_t frames);

/* -------------------------------------------------- data streams */
typedef struct vban_out {
    vban_name_t  name;
    uint32_t     frame;     /* frame counter / request id, auto-incremented */
    vban_addr_t  dest;
    vban_send_fn send;
    void        *send_ctx;
} vban_out_t;

void vban_out_init(vban_out_t *o, const char *name, const vban_addr_t *dest, vban_send_fn send, void *ctx);

/* Voicemeeter-style command text: UTF-8, bps index 18 (256 kbps), channel 0.
 * e.g. vban_send_text(&o, "Strip[0].Mute=1;", -1). Max 1436 bytes. */
int vban_send_text(vban_out_t *o, const char *utf8, int len);
int vban_send_text_ex(vban_out_t *o, unsigned bps_index, unsigned channel, unsigned format,
                      const void *data, size_t len);
/* Serial data; blocks over 1436 bytes are split with the multipart bit. */
int vban_send_serial(vban_out_t *o, unsigned bps_index, unsigned channel, unsigned bitmode,
                     unsigned type, const void *data, size_t len);
/* MIDI (one or more complete messages; SysEx of any size). */
int vban_send_midi(vban_out_t *o, const void *data, size_t len);
/* FRAME: split a buffer of any size (<= 65536 * 1436) into linked packets. */
int vban_send_frame(vban_out_t *o, unsigned mbps_index, unsigned stream_type, const void *data, size_t len);
/* PING0 request (ping NULL = header-only). Returns the request id used. */
int vban_send_ping(vban_out_t *o, const vban_ping0_t *self);

#ifdef __cplusplus
}
#endif
#endif /* VBAN_TX_H */
