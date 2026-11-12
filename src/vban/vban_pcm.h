/*
 * VBAN SDK - PCM sample conversion.
 *
 * All functions work on interleaved little-endian wire data (any alignment)
 * and native float / int16 buffers. `n` is the number of samples
 * (frames * channels). Conversions saturate; float -> int rounds to nearest.
 * Scaling: int16 <-> float uses 32768, int24 8388608, int32 2^31, u8 128.
 *
 * VBAN_DATATYPE_S10 / S12 have no storage layout defined by the spec and
 * return VBAN_E_UNSUPPORTED (packets can still be forwarded untouched).
 */
#ifndef VBAN_PCM_H
#define VBAN_PCM_H

#include <stddef.h>
#include <stdint.h>
#include "vban_proto.h"

#ifdef __cplusplus
extern "C" {
#endif

int vban_pcm_to_f32(const void *src, unsigned datatype, float *dst, size_t n);
int vban_pcm_from_f32(const float *src, void *dst, unsigned datatype, size_t n);
int vban_pcm_to_s16(const void *src, unsigned datatype, int16_t *dst, size_t n);
int vban_pcm_from_s16(const int16_t *src, void *dst, unsigned datatype, size_t n);

/* Any PCM type to any other (src and dst must not overlap unless equal type). */
int vban_pcm_convert(const void *src, unsigned src_type, void *dst, unsigned dst_type, size_t n);

/* Decode/encode straight between a packet payload and planar float channels.
 * dst[c] / src[c] may be NULL to skip (decode) or send silence (encode). */
int vban_pcm_to_f32_planar(const void *src, unsigned datatype, unsigned channels,
                           size_t frames, float *const *dst);
int vban_pcm_from_f32_planar(const float *const *src, unsigned channels, size_t frames,
                             void *dst, unsigned datatype);

/* Multiply wire samples in place by `gain` with saturation (e.g. routers/hubs). */
int vban_pcm_gain(void *data, unsigned datatype, size_t n, float gain);

/* Absolute peak (0..1+) of wire samples; negative error on bad type. */
float vban_pcm_peak(const void *src, unsigned datatype, size_t n);

/* Native float helpers. */
void vban_f32_deinterleave(const float *in, float *const *out, unsigned channels, size_t frames);
void vban_f32_interleave(const float *const *in, float *out, unsigned channels, size_t frames);

#ifdef __cplusplus
}
#endif
#endif /* VBAN_PCM_H */
