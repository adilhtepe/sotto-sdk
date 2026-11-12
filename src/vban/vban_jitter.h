/*
 * VBAN SDK - receive jitter buffer (playout-deadline design, ADR-001).
 *
 * Lock-free single-producer / single-consumer buffer between the network
 * thread (vban_jb_push) and the audio thread (vban_jb_pull_*). Neither side
 * ever blocks, allocates or takes a lock, so it is safe inside real-time
 * audio callbacks and ISRs.
 *
 * How it works
 *  - every packet is written at the slot its frame counter maps to
 *    (anchor + (counter - anchor_counter) * samples), not at the end of the
 *    queue. A packet that arrives late but before its playout time - a
 *    reordered packet or a redundant copy (FEC) - still fills its hole.
 *    Duplicates are recognised and dropped.
 *  - missing audio is concealed by the consumer at playout time (a short
 *    decay from the last sample, then a fade-in when audio resumes):
 *      * a hole with newer audio already buffered is a loss: the playout
 *        clock keeps running, latency does not change;
 *      * nothing newer buffered is starvation (late network): playout holds
 *        and plays concealment, so the delayed packets are still on time
 *        when they arrive, instead of "silence + re-prime" or a dropped hole.
 *  - adaptive latency: the target grows on starvation and late packets and
 *    shrinks after a stable period, never below largest pull + packet size
 *  - clock drift: the sender is master (spec); the consumer follows it with
 *    a tiny linear-interpolation resampler driven by the smoothed fill level
 *    (DRIFT_RESAMPLE), with single-frame slips (DRIFT_SLIP), or not at all
 *  - optional sample-rate conversion (stream rate -> device rate)
 *  - channel remap (mono -> all, extra channels dropped, missing = silence)
 *  - ring stored as float32 or int16 (half the RAM)
 *  - hardened against spoofed packets (unconfirmed counter jumps and rate
 *    changes are rejected)
 *
 * Memory: ring = capacity_frames * channels * (4 or 2) bytes, supplied by the
 * caller; vban_jb_t itself is ~1.2 KB (VBAN_JB_TAGS slot tags).
 * Usable buffering = min(capacity, (VBAN_JB_TAGS - 1) * samples per packet).
 */
#ifndef VBAN_JITTER_H
#define VBAN_JITTER_H

#include <stddef.h>
#include <stdint.h>
#include "vban_proto.h"
#include "vban_atomic.h"

#ifdef __cplusplus
extern "C" {
#endif

enum { VBAN_JB_F32 = 0, VBAN_JB_S16 = 1 };

/* Consecutive packets that must agree on a new sample rate before the buffer
 * switches to it (one spoofed packet must not retune the resampler). */
#ifndef VBAN_JB_RATE_CONFIRM
#define VBAN_JB_RATE_CONFIRM 3
#endif

/* Packet slots tracked (power of two). Bounds the buffer to
 * (VBAN_JB_TAGS - 1) packets: 255 x 32 samples = 170 ms @ 48 kHz. 4 B each. */
#ifndef VBAN_JB_TAGS
#define VBAN_JB_TAGS 256
#endif

/* Channels that get decay concealment; further channels conceal as silence. */
#ifndef VBAN_JB_PLC_CH
#define VBAN_JB_PLC_CH 8
#endif

enum { VBAN_JB_DRIFT_RESAMPLE = 0, VBAN_JB_DRIFT_SLIP = 1, VBAN_JB_DRIFT_NONE = 2 };

typedef struct vban_jb_config {
    uint16_t channels;       /* channels delivered to the consumer */
    uint32_t out_rate;       /* consumer sample rate; 0 = same as the stream */
    uint32_t target_frames;  /* initial latency target (0 = 2 x 256 frames) */
    uint32_t min_frames;     /* adaptive lower bound (0 = 64) */
    uint32_t max_frames;     /* adaptive upper bound (0 = capacity * 3/4) */
    uint8_t  adaptive;       /* 1 = grow on starvation / late packets, shrink when stable */
    uint8_t  drift;          /* VBAN_JB_DRIFT_* */
    uint8_t  storage;        /* VBAN_JB_F32 / VBAN_JB_S16 */
    uint32_t max_gap;        /* frame-counter jump treated as restart (0 = 32) */
} vban_jb_config_t;

typedef struct vban_jb_stats {
    uint32_t packets;        /* packets placed */
    uint32_t lost;           /* packets seen missing in the counter sequence (some may be repaired) */
    uint32_t late;           /* packets dropped because their playout time had passed */
    uint32_t repaired;       /* out-of-order / redundant packets that filled a hole in time */
    uint32_t dup;            /* duplicates dropped */
    uint32_t resets, overruns;
    uint32_t underruns;      /* starvation episodes (playout held) */
    uint32_t skips;          /* jumps forward after a burst (over-full buffer) */
    uint32_t rejected;       /* unconfirmed counter jumps / rate changes / anchor not ready */
    uint32_t concealed_frames;
    uint32_t fill_frames;    /* frames between playout position and newest audio */
    uint32_t target_frames;
    uint32_t stream_rate;
    float    ratio;          /* current resampling ratio (1.0 = none) */
} vban_jb_stats_t;

/* Mapping from frame counter to ring position. */
typedef struct vban_jb_anchor {
    uint32_t pos, frame, nbs, epoch, valid;
} vban_jb_anchor_t;

/* The same, published by the producer under a seqlock (never blocks). */
typedef struct vban_jb_anchor_sh {
    vban_atomic_u32 pos, frame, nbs, epoch, valid;
} vban_jb_anchor_sh_t;

typedef struct vban_jb {
    /* config (immutable after init) */
    void    *ring;
    uint32_t mask;           /* capacity_frames - 1 */
    uint16_t channels;
    uint8_t  storage, drift, adaptive;
    uint32_t out_rate, min_frames, max_frames;

    /* shared */
    vban_atomic_u32 w;       /* newest written position (end of the newest packet) */
    vban_atomic_u32 r;       /* playout position */
    vban_atomic_u32 in_rate;
    vban_atomic_u32 pkt_frames;
    vban_atomic_u32 reset_req;
    vban_atomic_u32 aseq;    /* seqlock over sh_cur / sh_prev: odd while being written */
    vban_atomic_u32 late_ev; /* producer: count of late drops (adaptive feedback) */
    vban_atomic_u32 tags[VBAN_JB_TAGS];
    vban_jb_anchor_sh_t sh_cur, sh_prev;

    /* producer-only */
    vban_seq_t seq;
    vban_jb_anchor_t cur;
    uint32_t epoch_val, w_own;
    uint32_t packets, lost, late, repaired, dup, resets, overruns, rejected;
    uint32_t rate_cand, rate_cnt;  /* a new sample rate needs VBAN_JB_RATE_CONFIRM packets in a row */
    uint8_t  force_anchor;

    /* consumer-only */
    vban_jb_anchor_t c_cur, c_prev;
    uint32_t seen_seq, seen_late;
    uint8_t  playing, stalled, concealing;
    uint32_t target;
    float    frac, ratio, fill_avg;
    uint32_t stable_frames, min_fill;
    uint32_t max_pull;       /* largest pull seen: with the packet size, the safe minimum target */
    uint32_t stall_frames, fade_left, concealed;
    float    last_good[VBAN_JB_PLC_CH], conc_gain;
    uint32_t underruns, skips;
} vban_jb_t;

/* Ring bytes needed; capacity_frames must be a power of two. */
size_t vban_jb_storage_bytes(uint32_t capacity_frames, unsigned channels, unsigned storage);

/* `cfg` may be NULL for defaults (2 channels, f32, adaptive, resample). */
int vban_jb_init(vban_jb_t *jb, const vban_jb_config_t *cfg, void *storage, uint32_t capacity_frames);

/* Producer: push a parsed AUDIO/PCM packet. Never blocks. */
int vban_jb_push(vban_jb_t *jb, const vban_packet_t *pkt);

/* Consumer: always writes `frames` interleaved frames. Returns the number of
 * frames played while the stream was active (stream audio or concealment);
 * 0 while priming (silence). */
size_t vban_jb_pull_f32(vban_jb_t *jb, float *out, size_t frames);
size_t vban_jb_pull_s16(vban_jb_t *jb, int16_t *out, size_t frames);

/* Any thread: ask the consumer to flush and re-prime on its next pull. */
void vban_jb_request_reset(vban_jb_t *jb);

/* Any thread (values are approximate snapshots). */
void vban_jb_get_stats(const vban_jb_t *jb, vban_jb_stats_t *st);
/* Current buffered latency in microseconds (consumer rate). */
uint32_t vban_jb_latency_us(const vban_jb_t *jb);

#ifdef __cplusplus
}
#endif
#endif /* VBAN_JITTER_H */
