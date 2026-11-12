/*
 * VBAN SDK - channel matrix mixer.
 *
 * n_in input channels x n_out output channels, float32. Effective gain of a
 * route = route * input gain * output gain * master (any mute -> 0). Gain
 * changes are ramped linearly over the next processed block (no zipper
 * noise / clicks). Silent routes cost nothing. Optional soft/hard clipper
 * and per-output peak meters.
 *
 * Threading: setters may be called from any thread while another thread is
 * processing; 32-bit float stores are atomic on every supported target and
 * the change is picked up at the start of the next block.
 *
 * Memory (caller-provided): vban_mixer_mem_size(n_in, n_out) bytes,
 * i.e. 8 * n_in * n_out + ~9 * (n_in + n_out) bytes (8x8: ~650 bytes).
 */
#ifndef VBAN_MIXER_H
#define VBAN_MIXER_H

#include <stddef.h>
#include <stdint.h>
#include "vban_atomic.h"

#ifdef __cplusplus
extern "C" {
#endif

enum { VBAN_LIMIT_NONE = 0, VBAN_LIMIT_HARD = 1, VBAN_LIMIT_SOFT = 2 };

typedef struct vban_mixer {
    uint16_t n_in, n_out;
    float   *route;      /* [n_in * n_out] user route gains (row = input) */
    float   *cur;        /* [n_in * n_out] effective gains being applied */
    float   *in_gain;    /* [n_in] */
    float   *out_gain;   /* [n_out] */
    float   *peak;       /* [n_out] */
    uint8_t *in_mute;    /* [n_in] */
    uint8_t *out_mute;   /* [n_out] */
    float    master;
    uint8_t  master_mute;
    uint8_t  limiter;
    vban_atomic_u32 dirty;
} vban_mixer_t;

/* Bytes of caller memory needed; 0 if the size is not representable. */
size_t vban_mixer_mem_size(unsigned n_in, unsigned n_out);
/* All routes start at 0 (silence) except the diagonal (i == o) at 1.0. */
int  vban_mixer_init(vban_mixer_t *mx, void *mem, size_t mem_size, unsigned n_in, unsigned n_out);

void vban_mixer_set_route(vban_mixer_t *mx, unsigned in, unsigned out, float gain);
void vban_mixer_set_input_gain(vban_mixer_t *mx, unsigned in, float gain);
void vban_mixer_set_input_mute(vban_mixer_t *mx, unsigned in, int mute);
void vban_mixer_set_output_gain(vban_mixer_t *mx, unsigned out, float gain);
void vban_mixer_set_output_mute(vban_mixer_t *mx, unsigned out, int mute);
void vban_mixer_set_master(vban_mixer_t *mx, float gain, int mute);
void vban_mixer_set_limiter(vban_mixer_t *mx, int mode);
void vban_mixer_clear_routes(vban_mixer_t *mx);

/* dB helpers */
float vban_db_to_gain(float db);
float vban_gain_to_db(float gain);

/* Planar: in[i] / out[o] point to `frames` floats; in[i] may be NULL (silence). */
void vban_mixer_process(vban_mixer_t *mx, const float *const *in, float *const *out, size_t frames);

/* Interleaved: `in` has n_in channels, `out` n_out channels per frame. */
void vban_mixer_process_interleaved(vban_mixer_t *mx, const float *in, float *out, size_t frames);

/* Peak since the last call (and reset). */
float vban_mixer_take_peak(vban_mixer_t *mx, unsigned out);

#ifdef __cplusplus
}
#endif
#endif /* VBAN_MIXER_H */
