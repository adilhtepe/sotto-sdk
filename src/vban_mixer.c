/* VBAN SDK - matrix mixer. See vban/vban_mixer.h. */
#include "vban/vban_mixer.h"
#include <limits.h>
#include <math.h>
#include <stdint.h>
#include <string.h>

#define ALIGN8(x) (((x) + 7u) & ~(size_t)7u)

size_t vban_mixer_mem_size(unsigned n_in, unsigned n_out)
{
    size_t m;
    /* 0 = too large to represent (e.g. 65535 x 65535 on a 32-bit target):
     * without this the sum wraps and init accepts a buffer that is too small */
    if (n_in && n_out > (SIZE_MAX / 16 - 256) / n_in) return 0;
    m = (size_t)n_in * n_out;
    return ALIGN8(m * 4) * 2 + ALIGN8((size_t)n_in * 4) + ALIGN8((size_t)n_out * 4) * 2 +
           ALIGN8(n_in) + ALIGN8(n_out);
}

int vban_mixer_init(vban_mixer_t *mx, void *mem, size_t size, unsigned n_in, unsigned n_out)
{
    uint8_t *p = (uint8_t *)mem;
    size_t m = (size_t)n_in * n_out;
    unsigned i;
#if UINT_MAX > 0xFFFFu /* vacuous with a 16-bit int (AVR); avoids -Wtype-limits */
    if (n_in > 65535 || n_out > 65535) return -1; /* before the size math */
#endif
    if (!mx || !mem || n_in == 0 || n_out == 0 || vban_mixer_mem_size(n_in, n_out) == 0 ||
        size < vban_mixer_mem_size(n_in, n_out) || ((uintptr_t)mem & 3))
        return -1;
    memset(mem, 0, vban_mixer_mem_size(n_in, n_out));
    memset(mx, 0, sizeof *mx);
    mx->n_in = (uint16_t)n_in;
    mx->n_out = (uint16_t)n_out;
    mx->route    = (float *)p;   p += ALIGN8(m * 4);
    mx->cur      = (float *)p;   p += ALIGN8(m * 4);
    mx->in_gain  = (float *)p;   p += ALIGN8((size_t)n_in * 4);
    mx->out_gain = (float *)p;   p += ALIGN8((size_t)n_out * 4);
    mx->peak     = (float *)p;   p += ALIGN8((size_t)n_out * 4);
    mx->in_mute  = p;            p += ALIGN8(n_in);
    mx->out_mute = p;
    for (i = 0; i < n_in; ++i) mx->in_gain[i] = 1.0f;
    for (i = 0; i < n_out; ++i) mx->out_gain[i] = 1.0f;
    for (i = 0; i < n_in && i < n_out; ++i) mx->route[(size_t)i * n_out + i] = 1.0f;
    mx->master = 1.0f;
    mx->limiter = VBAN_LIMIT_NONE;
    mx->dirty = 1;
    return 0;
}

#define TOUCH(mx) vban_store_rel(&(mx)->dirty, 1)

void vban_mixer_set_route(vban_mixer_t *mx, unsigned i, unsigned o, float g)
{ if (i < mx->n_in && o < mx->n_out) { mx->route[(size_t)i * mx->n_out + o] = g; TOUCH(mx); } }
void vban_mixer_set_input_gain(vban_mixer_t *mx, unsigned i, float g)
{ if (i < mx->n_in) { mx->in_gain[i] = g; TOUCH(mx); } }
void vban_mixer_set_input_mute(vban_mixer_t *mx, unsigned i, int m)
{ if (i < mx->n_in) { mx->in_mute[i] = (uint8_t)(m != 0); TOUCH(mx); } }
void vban_mixer_set_output_gain(vban_mixer_t *mx, unsigned o, float g)
{ if (o < mx->n_out) { mx->out_gain[o] = g; TOUCH(mx); } }
void vban_mixer_set_output_mute(vban_mixer_t *mx, unsigned o, int m)
{ if (o < mx->n_out) { mx->out_mute[o] = (uint8_t)(m != 0); TOUCH(mx); } }
void vban_mixer_set_master(vban_mixer_t *mx, float g, int m)
{ mx->master = g; mx->master_mute = (uint8_t)(m != 0); TOUCH(mx); }
void vban_mixer_set_limiter(vban_mixer_t *mx, int mode)
{ mx->limiter = (uint8_t)mode; }
void vban_mixer_clear_routes(vban_mixer_t *mx)
{ memset(mx->route, 0, (size_t)mx->n_in * mx->n_out * sizeof(float)); TOUCH(mx); }

float vban_db_to_gain(float db) { return db <= -120.0f ? 0.0f : powf(10.0f, db / 20.0f); }
float vban_gain_to_db(float g)  { return g <= 1e-6f ? -120.0f : 20.0f * log10f(g); }

static inline float target_gain(const vban_mixer_t *mx, unsigned i, unsigned o)
{
    if (mx->master_mute || mx->in_mute[i] || mx->out_mute[o]) return 0.0f;
    return mx->route[(size_t)i * mx->n_out + o] * mx->in_gain[i] * mx->out_gain[o] * mx->master;
}

/* Soft knee at 0.8, asymptote 1.0, slope continuous at the knee. */
static inline float soft_clip(float x)
{
    float a = fabsf(x), z, y;
    if (a <= 0.8f) return x;
    z = (a - 0.8f) * 5.0f;
    y = 0.8f + 0.2f * (z / (1.0f + z));
    return x < 0 ? -y : y;
}

static void finish_output(vban_mixer_t *mx, unsigned o, float *y, size_t n)
{
    size_t k;
    float pk = mx->peak[o];
    if (mx->limiter == VBAN_LIMIT_SOFT) {
        for (k = 0; k < n; ++k) y[k] = soft_clip(y[k]);
    } else if (mx->limiter == VBAN_LIMIT_HARD) {
        for (k = 0; k < n; ++k) y[k] = y[k] > 1.0f ? 1.0f : (y[k] < -1.0f ? -1.0f : y[k]);
    }
    for (k = 0; k < n; ++k) { float a = fabsf(y[k]); pk = a > pk ? a : pk; }
    mx->peak[o] = pk;
}

void vban_mixer_process(vban_mixer_t *mx, const float *const *in, float *const *out, size_t n)
{
    unsigned i, o, ni = mx->n_in, no = mx->n_out;
    int dirty = vban_load_acq(&mx->dirty) != 0;
    float inv_n = n ? 1.0f / (float)n : 0.0f;
    if (dirty) vban_store_rlx(&mx->dirty, 0);

    for (o = 0; o < no; ++o) {
        float *y = out[o];
        int first = 1;
        if (!y) {
            if (dirty) for (i = 0; i < ni; ++i) mx->cur[(size_t)i * no + o] = target_gain(mx, i, o);
            continue;
        }
        for (i = 0; i < ni; ++i) {
            const float *x = in[i];
            float *gc = &mx->cur[(size_t)i * no + o];
            float g0 = *gc, g1 = dirty ? target_gain(mx, i, o) : g0;
            size_t k;
            if (g0 == 0.0f && g1 == 0.0f) continue;
            if (!x) { *gc = g1; continue; }
            if (g0 == g1) {
                if (first) for (k = 0; k < n; ++k) y[k] = x[k] * g0;
                else       for (k = 0; k < n; ++k) y[k] += x[k] * g0;
            } else {
                float d = (g1 - g0) * inv_n, g = g0;
                if (first) for (k = 0; k < n; ++k) { g += d; y[k] = x[k] * g; }
                else       for (k = 0; k < n; ++k) { g += d; y[k] += x[k] * g; }
                *gc = g1;
            }
            first = 0;
        }
        if (first) memset(y, 0, n * sizeof(float));
        else finish_output(mx, o, y, n);
    }
}

void vban_mixer_process_interleaved(vban_mixer_t *mx, const float *in, float *out, size_t n)
{
    unsigned i, o, ni = mx->n_in, no = mx->n_out;
    int dirty = vban_load_acq(&mx->dirty) != 0;
    float inv_n = n ? 1.0f / (float)n : 0.0f;
    size_t k;
    if (dirty) vban_store_rlx(&mx->dirty, 0);
    memset(out, 0, n * no * sizeof(float));

    for (i = 0; i < ni; ++i) {
        for (o = 0; o < no; ++o) {
            float *gc = &mx->cur[(size_t)i * no + o];
            float g0 = *gc, g1 = dirty ? target_gain(mx, i, o) : g0;
            const float *x = in + i;
            float *y = out + o;
            if (g0 == 0.0f && g1 == 0.0f) continue;
            if (g0 == g1) {
                for (k = 0; k < n; ++k) y[k * no] += x[k * ni] * g0;
            } else {
                float d = (g1 - g0) * inv_n, g = g0;
                for (k = 0; k < n; ++k) { g += d; y[k * no] += x[k * ni] * g; }
                *gc = g1;
            }
        }
    }
    for (o = 0; o < no; ++o) {
        float pk = mx->peak[o];
        for (k = 0; k < n; ++k) {
            float *v = &out[k * no + o];
            if (mx->limiter == VBAN_LIMIT_SOFT) *v = soft_clip(*v);
            else if (mx->limiter == VBAN_LIMIT_HARD) *v = *v > 1.0f ? 1.0f : (*v < -1.0f ? -1.0f : *v);
            pk = fabsf(*v) > pk ? fabsf(*v) : pk;
        }
        mx->peak[o] = pk;
    }
}

float vban_mixer_take_peak(vban_mixer_t *mx, unsigned o)
{
    float p;
    if (o >= mx->n_out) return 0.0f;
    p = mx->peak[o];
    mx->peak[o] = 0.0f;
    return p;
}
