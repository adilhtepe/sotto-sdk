/* SPDX-License-Identifier: GPL-2.0 OR MIT */
/*
 * Receive ring of the snd-vban kernel module, shared verbatim with userspace
 * tests (bench/netsim.c --engine kring, tests: parity) so the exact kernel
 * code can be measured against the SDK's float jitter buffer.
 *
 * Integer-only. Not thread-safe: the caller serialises kring_place() (network
 * side) and kring_pull() (playout side) - snd-vban holds rx_lock around both.
 *
 * Behaviour (same model as src/vban_jitter.c, ADR-001):
 *  - packets land in the slot their frame counter maps to; duplicates and
 *    packets whose playout time passed are dropped, late ones fill holes
 *  - holes with newer audio after them are concealed (Q15 decay) while the
 *    clock keeps running; starvation holds playout; audio fades back in
 *  - fixed target (no adaptive growth); sender clock drift followed by an
 *    integer (Q16) linear-interpolation resampler, +-0.5 %, like the SDK's;
 *    a large backlog is skipped in one jump
 *  - while not active (capture closed) packets are only counted; activation
 *    re-anchors on the live stream
 */
#ifndef VBAN_KRING_H
#define VBAN_KRING_H

#ifdef __KERNEL__
#include <linux/types.h>
#include <linux/string.h>
#include <linux/minmax.h>
#include <linux/unaligned.h>
#else
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>
typedef uint8_t u8;
typedef uint16_t u16;
typedef int16_t s16;
typedef uint32_t u32;
typedef int32_t s32;
typedef uint64_t u64;
typedef uint16_t __le16;
static inline u16 get_unaligned_le16(const void *p)
{
	const u8 *b = (const u8 *)p;
	return (u16)(b[0] | (b[1] << 8));
}
#if defined(__BYTE_ORDER__) && __BYTE_ORDER__ == __ORDER_BIG_ENDIAN__
#define cpu_to_le16(x) ((u16)((((u16)(x)) >> 8) | (((u16)(x)) << 8)))
#else
#define cpu_to_le16(x) ((u16)(x))
#endif
#define min_t(t, a, b) ((t)(a) < (t)(b) ? (t)(a) : (t)(b))
#endif

#include "vban/vban_proto.h"

#define KRING_FRAMES      16384u     /* power of two */
#define KRING_MASK        (KRING_FRAMES - 1u)
#define KRING_SLOTS       512u       /* packet tags, power of two */
#define KRING_SLOT_MASK   (KRING_SLOTS - 1u)
#define KRING_MAX_CH      8
#define KRING_FADE_FRAMES 64
#define KRING_DECAY_Q15   31130      /* 0.95 per frame */

struct vban_kring {
	/* config */
	s16 *ring;                       /* KRING_FRAMES * channels, native endian */
	int channels, fpp, target;
	/* state */
	u32 tags[KRING_SLOTS];           /* frame counter + 1 of the packet in the slot */
	vban_seq_t seq;
	bool active;                     /* playout running: place packets; else only count */
	bool anchored, playing, stalled;
	u32 base_f, base_pos, nbs, wmax, rpos;
	s32 fill_avg;                    /* Q8 */
	u32 phase;                       /* Q16 fractional read position, 0..65535 */
	s32 ratio_q16;                   /* input frames per output frame, Q16 (65536 = 1.0) */
	u32 stall_frames, fade_left, conc_gain;
	s16 last[KRING_MAX_CH];
	/* stats */
	u64 packets, lost, late, dup, repaired, rejected, idle, concealed, underruns, skips;
	u64 concealed_loss, concealed_stall, reprimes;
};

static inline void kring_init(struct vban_kring *k, s16 *ring, int channels, int fpp, int target)
{
	memset(k, 0, sizeof(*k));
	k->ring = ring;
	k->channels = channels;
	k->fpp = fpp;
	k->target = target;
	k->conc_gain = 1u << 15;
	k->ratio_q16 = 1 << 16;
	vban_seq_init(&k->seq, 32);
}

/* Playout starts: re-anchor on the next packet (fresh timeline). */
static inline void kring_start(struct vban_kring *k)
{
	k->anchored = false;
	k->playing = false;
	k->stalled = false;
	k->stall_frames = 0;
	k->active = true;
}

static inline void kring_stop(struct vban_kring *k)
{
	k->active = false;
}

static inline s32 kring_fill(const struct vban_kring *k)
{
	return k->anchored ? (s32)(k->wmax - k->rpos) : 0;
}

/* Is ring position idx present? */
static inline bool kring_frame_valid(const struct vban_kring *k, u32 idx)
{
	u32 f;

	if (!k->anchored || (s32)(k->wmax - idx) <= 0 || (s32)(idx - k->base_pos) < 0)
		return false;
	f = k->base_f + (idx - k->base_pos) / k->nbs;
	return k->tags[f & KRING_SLOT_MASK] == f + 1;
}

/* Place a validated S16, `channels`-channel packet in its slot. */
static inline void kring_place(struct vban_kring *k, const vban_packet_t *p)
{
	u32 lost = 0, f = p->nu_frame, n = p->samples, pos, end, skip, win, i;
	vban_seq_result_t sr = vban_seq_update(&k->seq, f, &lost);
	const u8 *src;
	s32 d;
	int c;

	if (sr == VBAN_SEQ_SUSPECT) {
		k->rejected++;
		return;
	}
	if (!k->active) {
		/* nobody is playing the ring: placing packets would only fill it and
		 * leave the timeline stale for the next start */
		k->idle++;
		return;
	}
	if (!k->anchored || sr == VBAN_SEQ_FIRST || sr == VBAN_SEQ_RESET || n != k->nbs) {
		k->base_pos = (k->anchored && (s32)(k->wmax - k->rpos) > 0) ? k->wmax : k->rpos;
		k->base_f = f;
		k->nbs = n;
		k->anchored = true;
		memset(k->tags, 0, sizeof(k->tags));
	}
	d = (s32)(f - k->base_f);
	if (d < 0) {
		k->late++;
		return;
	}
	pos = k->base_pos + (u32)d * k->nbs;
	end = pos + n;
	if (k->tags[f & KRING_SLOT_MASK] == f + 1) {
		k->dup++;
		return;
	}
	if ((s32)(end - k->rpos) <= 0) {
		k->late++;
		return;
	}
	win = min_t(u32, KRING_FRAMES - VBAN_SAMPLES_MAX, (KRING_SLOTS - 1) * n);
	if (end - k->rpos > win)
		return;                  /* too far ahead to keep */
	skip = (s32)(pos - k->rpos) < 0 ? k->rpos - pos : 0;
	src = p->data + (size_t)skip * k->channels * 2;
	for (i = skip; i < n; ++i) {
		s16 *dst = k->ring + (size_t)((pos + i) & KRING_MASK) * k->channels;

		for (c = 0; c < k->channels; ++c, src += 2)
			dst[c] = (s16)get_unaligned_le16(src);
	}
	k->tags[f & KRING_SLOT_MASK] = f + 1;
	k->packets++;
	if (sr == VBAN_SEQ_GAP)
		k->lost += lost;
	if (sr == VBAN_SEQ_LATE)
		k->repaired++;
	if ((s32)(end - k->wmax) > 0)
		k->wmax = end;
}

static inline void kring_conceal_frame(struct vban_kring *k, __le16 *o)
{
	int c;

	k->conc_gain = (k->conc_gain * KRING_DECAY_Q15) >> 15;
	for (c = 0; c < k->channels; ++c) {
		s32 v = c < KRING_MAX_CH ? ((s32)k->last[c] * (s32)k->conc_gain) >> 15 : 0;

		o[c] = cpu_to_le16((u16)(s16)v);
	}
	k->concealed++;
	if (k->fade_left == 0)
		k->fade_left = KRING_FADE_FRAMES;  /* fade in when audio resumes */
}

/* Produce n frames (interleaved S16_LE) into o. Returns frames played while
 * the stream was active (audio or concealment); 0 while priming (silence). */
static inline u32 kring_pull(struct vban_kring *k, __le16 *o, u32 n)
{
	u32 i;
	int c;

	if (!k->playing) {
		if (!k->anchored || (s32)(k->wmax - k->rpos) < k->target) {
			memset(o, 0, (size_t)n * k->channels * 2);
			return 0;
		}
		k->playing = true;
		k->stalled = false;
		k->fill_avg = (s32)(k->wmax - k->rpos) << 8;
		k->phase = 0;
		k->ratio_q16 = 1 << 16;
	}
	for (i = 0; i < n; ++i, o += k->channels) {
		u32 idx = k->rpos;

		if (kring_frame_valid(k, idx) &&
		    !(k->stalled && (s32)(k->wmax - idx) < (s32)(k->nbs + (u32)k->fpp))) {
			const s16 *a = k->ring + (size_t)(idx & KRING_MASK) * k->channels;
			const s16 *b = kring_frame_valid(k, idx + 1) ?
				k->ring + (size_t)((idx + 1) & KRING_MASK) * k->channels : a;
			s32 ph = (s32)(k->phase >> 1);   /* Q15: keeps (b - a) * ph inside s32 */
			u32 adv;

			k->stalled = false;
			for (c = 0; c < k->channels; ++c) {
				s32 v = a[c] + ((((s32)b[c] - (s32)a[c]) * ph) >> 15);

				if (k->fade_left)
					v = v * (KRING_FADE_FRAMES + 1 - (s32)k->fade_left) / (KRING_FADE_FRAMES + 1);
				o[c] = cpu_to_le16((u16)(s16)v);
				if (c < KRING_MAX_CH)
					k->last[c] = (s16)v;
			}
			if (k->fade_left)
				k->fade_left--;
			k->conc_gain = 1u << 15;
			adv = k->phase + (u32)k->ratio_q16;
			k->rpos += adv >> 16;
			k->phase = adv & 0xFFFFu;
		} else if ((s32)(k->wmax - idx) > 0 &&
			   (!k->stalled || (s32)(k->wmax - idx) >= (s32)(k->nbs + (u32)k->fpp))) {
			/* loss: newer audio exists, keep the clock running. While stalled,
			 * a hole at the head must not block recovery once enough newer
			 * audio is buffered (it used to wait for the lost packet forever,
			 * until the 170 ms re-prime: netsim "Wi-Fi congested" 14 re-primes). */
			k->stalled = false;
			kring_conceal_frame(k, o);
			k->concealed_loss++;
			k->rpos++;
			k->phase = 0;
		} else {
			if (!k->stalled) {               /* starvation: hold playout */
				k->stalled = true;
				k->underruns++;
			}
			kring_conceal_frame(k, o);
			k->concealed_stall++;
			if (++k->stall_frames > KRING_FRAMES / 2) {
				k->playing = false;      /* stream stopped: re-prime */
				k->stall_frames = 0;
				k->reprimes++;
			}
		}
	}
	if (!k->stalled)
		k->stall_frames = 0;

	/* Sender clock drift: the playout rate follows the smoothed fill level
	 * with the SDK's control law (ratio = 1 + 0.004 * (avg - target) / target,
	 * limited to +-0.5 %), applied by the Q16 interpolation above. This
	 * replaced one-frame slips, which netsim showed cannot be both quiet and
	 * quick: rate-limited they kept a stall's extra latency for seconds (+1.5
	 * ms vs the SDK), fast enough to recover they clicked 54 times/s on
	 * "Wi-Fi congested". The average is slow (1/256 per pull) so network
	 * jitter is not mistaken for drift. */
	if (k->playing && !k->stalled) {
		s32 fill = (s32)(k->wmax - k->rpos);
		s32 base = k->target > 64 ? k->target : 64, adj;

		k->fill_avg += fill - (k->fill_avg >> 8);
		if (fill > 2 * k->target + 4 * (s32)k->nbs) {
			/* far too full (burst after a network stall): jump */
			k->rpos = k->wmax - (u32)k->target;
			k->fill_avg = k->target << 8;
			k->phase = 0;
			k->fade_left = KRING_FADE_FRAMES;
			k->skips++;
		}
		adj = ((k->fill_avg >> 8) - k->target) * 262 / base;   /* 262 = 0.004 * 65536 */
		if (adj > 328)                                           /* 328 = 0.005 * 65536 */
			adj = 328;
		if (adj < -328)
			adj = -328;
		k->ratio_q16 = (1 << 16) + adj;
	}
	return k->playing ? n : 0;
}

#endif /* VBAN_KRING_H */
