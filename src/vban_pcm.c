/* VBAN SDK - PCM conversion. See vban/vban_pcm.h.
 *
 * Loads/stores are written byte-wise (endian- and alignment-proof); GCC and
 * Clang fuse them into single loads on little-endian targets, and the type
 * switch sits outside every loop so the inner loops vectorize. */
#include "vban/vban_pcm.h"
#include <math.h>
#include <string.h>

/* ----------------------------------------------------------- primitives */

static inline float ld_f32(const uint8_t *p)
{
    uint32_t u = vban_rd32(p);
    float f;
    memcpy(&f, &u, 4);
    return f;
}

static inline void st_f32(uint8_t *p, float f)
{
    uint32_t u;
    memcpy(&u, &f, 4);
    vban_wr32(p, u);
}

#if VBAN_ENABLE_F64
static inline double ld_f64(const uint8_t *p)
{
    uint64_t u = (uint64_t)vban_rd32(p) | ((uint64_t)vban_rd32(p + 4) << 32);
    double d;
    memcpy(&d, &u, 8);
    return d;
}

static inline void st_f64(uint8_t *p, double d)
{
    uint64_t u;
    memcpy(&u, &d, 8);
    vban_wr32(p, (uint32_t)u);
    vban_wr32(p + 4, (uint32_t)(u >> 32));
}
#endif

static inline int32_t ld_s24(const uint8_t *p)
{
    /* sign-extend via shift on the unsigned value (no UB) */
    uint32_t u = (uint32_t)p[0] << 8 | (uint32_t)p[1] << 16 | (uint32_t)p[2] << 24;
    return (int32_t)u >> 8;
}

static inline void st_s24(uint8_t *p, int32_t v)
{
    p[0] = (uint8_t)v; p[1] = (uint8_t)(v >> 8); p[2] = (uint8_t)(v >> 16);
}

/* NaN-safe: NaN maps to `lo` instead of flowing into a float->int
 * conversion (undefined behaviour in C). */
static inline float clampf(float v, float lo, float hi) { return v >= lo ? (v <= hi ? v : hi) : lo; }

/* Float samples arriving from the network: NaN -> 0, +-Inf / huge -> +-1 (the
 * spec's normal range), so hostile packets cannot poison filters downstream
 * or blast full-scale noise (SEC-005). */
static inline float san(float v) { return v == v ? clampf(v, -1.0f, 1.0f) : 0.0f; }

#if defined(_MSC_VER)
#define VBAN_RESTRICT __restrict
#define VBAN_NOINLINE __declspec(noinline)
#else
#define VBAN_RESTRICT restrict
#if defined(__GNUC__) || defined(__clang__)
#define VBAN_NOINLINE __attribute__((noinline)) /* keep `restrict` scoped: inlined, GCC drops it */
#else
#define VBAN_NOINLINE
#endif
#endif

/* Same for a little-endian f32 on the wire, done entirely on the integer bits
 * so the loop vectorizes: |x| > 1 (incl. Inf) -> +-1, NaN -> 0. Measured
 * (x86-64, GCC 15, -O2, restrict pointers): plain copy 0.20 ns/sample, this
 * 0.49, a float `v == v` test 0.98; without `restrict` nothing vectorizes
 * (0.8-2.8 ns) because GCC -O2 will not add a runtime alias check. */
static inline uint32_t f32_bits_san(uint32_t u)
{
    uint32_t a = u & 0x7FFFFFFFu;
    u = a > 0x3F800000u ? ((u & 0x80000000u) | 0x3F800000u) : u;
    return a >= 0x7F800001u ? 0u : u;
}

static inline float ld_f32_san(const uint8_t *p)
{
    uint32_t u = f32_bits_san(vban_rd32(p));
    float f;
    memcpy(&f, &u, 4);
    return f;
}

static VBAN_NOINLINE void f32_wire_to_f32(const uint8_t *VBAN_RESTRICT s, float *VBAN_RESTRICT d, size_t n)
{
    size_t i;
    for (i = 0; i < n; ++i) d[i] = ld_f32_san(s + 4 * i);
}

/* Two roundings, chosen by measurement (x86-64, GCC 15, -O2 -fno-trapping-math):
 *  - round_vec: branchless copysign + truncation. Lets the u8/s16 loops
 *    auto-vectorize: f32->s16 2.12 -> 1.14 ns/sample.
 *  - round_scl: lrintf (one cvtss2si). Faster where the loop cannot vectorize
 *    anyway (in-place gain): copysign there regressed gain 3.4 -> 4.6 ns.
 *  - s24 is converted in two passes (vectorized rounding into an int32
 *    block, then 3-byte packing): 2.71 -> 2.17 ns/sample.
 * Without -fno-trapping-math the float clamp blocks vectorization and both
 * behave like scalar code. */
static inline int32_t round_vec(float v, float lo, float hi, int32_t imax)
{
    int32_t i;
    v = clampf(v, lo, hi);
    i = (int32_t)(v + copysignf(0.5f, v));
    return i > imax ? imax : i; /* hi + 0.5 may round up one ulp */
}

static inline int32_t round_scl(float v, float lo, float hi)
{
    return (int32_t)lrintf(clampf(v, lo, hi)); /* exact at hi: no extra clamp (+0.7 ns measured) */
}

/* float (full-scale +-1) -> integer of `scale`, rounded, saturated. */
static inline int32_t f2i(float v, float scale, float maxv)
{
    return round_vec(v * scale, -scale, maxv, (int32_t)maxv);
}

static VBAN_NOINLINE void f32_wire_to_s16(const uint8_t *VBAN_RESTRICT s, int16_t *VBAN_RESTRICT d, size_t n)
{
    size_t i;
    for (i = 0; i < n; ++i) {
        /* input already in [-1, 1]: only +1.0 * 32768 needs clamping */
        float x = ld_f32_san(s + 4 * i) * 32768.0f;
        int32_t v = (int32_t)(x + copysignf(0.5f, x));
        d[i] = (int16_t)(v > 32767 ? 32767 : v);
    }
}

static inline int32_t f2i_s24(float v)
{
    return round_scl(v * 8388608.0f, -8388608.0f, 8388607.0f);
}

static void f32_to_s24(const float *src, uint8_t *d, size_t n)
{
    int32_t tmp[128];
    while (n) {
        size_t k = n < 128 ? n : 128, i;
        for (i = 0; i < k; ++i) tmp[i] = f2i(src[i], 8388608.0f, 8388607.0f);
        for (i = 0; i < k; ++i) st_s24(d + 3 * i, tmp[i]);
        src += k; d += 3 * k; n -= k;
    }
}

static inline int32_t f2i32(float v)
{
    /* 2147483647 is not representable as float; clamp in float domain to
     * the largest float below 2^31 (2147483520). */
    float x = v * 2147483648.0f;
    if (x >= 2147483520.0f) return INT32_MAX;
    if (x <= -2147483648.0f) return INT32_MIN;
    return (int32_t)lrintf(x);
}

static int valid_type(unsigned dt)
{
    if (dt > VBAN_DATATYPE_F64) return 0;
#if !VBAN_ENABLE_F64
    if (dt == VBAN_DATATYPE_F64) return 0;
#endif
    return 1;
}

/* ---------------------------------------------------------- to float */

int vban_pcm_to_f32(const void *src_, unsigned dt, float *dst, size_t n)
{
    const uint8_t *s = (const uint8_t *)src_;
    size_t i;
    if (!src_ || !dst) return VBAN_E_ARG;
    switch (dt) {
    case VBAN_DATATYPE_U8:
        for (i = 0; i < n; ++i) dst[i] = ((int)s[i] - 128) * (1.0f / 128.0f);
        return VBAN_OK;
    case VBAN_DATATYPE_S16:
        for (i = 0; i < n; ++i) dst[i] = (int16_t)vban_rd16(s + 2 * i) * (1.0f / 32768.0f);
        return VBAN_OK;
    case VBAN_DATATYPE_S24:
        for (i = 0; i < n; ++i) dst[i] = ld_s24(s + 3 * i) * (1.0f / 8388608.0f);
        return VBAN_OK;
    case VBAN_DATATYPE_S32:
        for (i = 0; i < n; ++i) dst[i] = (float)(int32_t)vban_rd32(s + 4 * i) * (1.0f / 2147483648.0f);
        return VBAN_OK;
    case VBAN_DATATYPE_F32:
        f32_wire_to_f32(s, dst, n);
        return VBAN_OK;
#if VBAN_ENABLE_F64
    case VBAN_DATATYPE_F64:
        for (i = 0; i < n; ++i) dst[i] = san((float)ld_f64(s + 8 * i));
        return VBAN_OK;
#endif
    default:
        return VBAN_E_UNSUPPORTED;
    }
}

int vban_pcm_from_f32(const float *src, void *dst_, unsigned dt, size_t n)
{
    uint8_t *d = (uint8_t *)dst_;
    size_t i;
    if (!src || !dst_) return VBAN_E_ARG;
    switch (dt) {
    case VBAN_DATATYPE_U8:
        for (i = 0; i < n; ++i) d[i] = (uint8_t)(f2i(src[i], 128.0f, 127.0f) + 128);
        return VBAN_OK;
    case VBAN_DATATYPE_S16:
        for (i = 0; i < n; ++i) vban_wr16(d + 2 * i, (uint16_t)(int16_t)f2i(src[i], 32768.0f, 32767.0f));
        return VBAN_OK;
    case VBAN_DATATYPE_S24:
        f32_to_s24(src, d, n);
        return VBAN_OK;
    case VBAN_DATATYPE_S32:
        for (i = 0; i < n; ++i) vban_wr32(d + 4 * i, (uint32_t)f2i32(src[i]));
        return VBAN_OK;
    case VBAN_DATATYPE_F32:
        for (i = 0; i < n; ++i) st_f32(d + 4 * i, src[i]);
        return VBAN_OK;
#if VBAN_ENABLE_F64
    case VBAN_DATATYPE_F64:
        for (i = 0; i < n; ++i) st_f64(d + 8 * i, (double)src[i]);
        return VBAN_OK;
#endif
    default:
        return VBAN_E_UNSUPPORTED;
    }
}

/* ---------------------------------------------------------- int16 paths */

int vban_pcm_to_s16(const void *src_, unsigned dt, int16_t *dst, size_t n)
{
    const uint8_t *s = (const uint8_t *)src_;
    size_t i;
    if (!src_ || !dst) return VBAN_E_ARG;
    switch (dt) {
    case VBAN_DATATYPE_U8:
        for (i = 0; i < n; ++i) dst[i] = (int16_t)(((int)s[i] - 128) * 256);
        return VBAN_OK;
    case VBAN_DATATYPE_S16:
        for (i = 0; i < n; ++i) dst[i] = (int16_t)vban_rd16(s + 2 * i);
        return VBAN_OK;
    case VBAN_DATATYPE_S24:
        /* round to nearest, saturate at the top */
        for (i = 0; i < n; ++i) {
            int32_t v = (ld_s24(s + 3 * i) + 128) >> 8;
            dst[i] = (int16_t)(v > 32767 ? 32767 : v);
        }
        return VBAN_OK;
    case VBAN_DATATYPE_S32:
        for (i = 0; i < n; ++i) {
            int64_t v = ((int64_t)(int32_t)vban_rd32(s + 4 * i) + 32768) >> 16;
            dst[i] = (int16_t)(v > 32767 ? 32767 : v);
        }
        return VBAN_OK;
    case VBAN_DATATYPE_F32:
        f32_wire_to_s16(s, dst, n);
        return VBAN_OK;
#if VBAN_ENABLE_F64
    case VBAN_DATATYPE_F64:
        for (i = 0; i < n; ++i) dst[i] = (int16_t)f2i(san((float)ld_f64(s + 8 * i)), 32768.0f, 32767.0f);
        return VBAN_OK;
#endif
    default:
        return VBAN_E_UNSUPPORTED;
    }
}

int vban_pcm_from_s16(const int16_t *src, void *dst_, unsigned dt, size_t n)
{
    uint8_t *d = (uint8_t *)dst_;
    size_t i;
    if (!src || !dst_) return VBAN_E_ARG;
    switch (dt) {
    case VBAN_DATATYPE_U8:
        for (i = 0; i < n; ++i) {
            int v = (src[i] + 128) >> 8;
            d[i] = (uint8_t)((v > 127 ? 127 : v) + 128);
        }
        return VBAN_OK;
    case VBAN_DATATYPE_S16:
        for (i = 0; i < n; ++i) vban_wr16(d + 2 * i, (uint16_t)src[i]);
        return VBAN_OK;
    case VBAN_DATATYPE_S24:
        for (i = 0; i < n; ++i) st_s24(d + 3 * i, (int32_t)src[i] * 256);
        return VBAN_OK;
    case VBAN_DATATYPE_S32:
        for (i = 0; i < n; ++i) vban_wr32(d + 4 * i, (uint32_t)((int32_t)src[i] * 65536));
        return VBAN_OK;
    case VBAN_DATATYPE_F32:
        for (i = 0; i < n; ++i) st_f32(d + 4 * i, src[i] * (1.0f / 32768.0f));
        return VBAN_OK;
#if VBAN_ENABLE_F64
    case VBAN_DATATYPE_F64:
        for (i = 0; i < n; ++i) st_f64(d + 8 * i, src[i] * (1.0 / 32768.0));
        return VBAN_OK;
#endif
    default:
        return VBAN_E_UNSUPPORTED;
    }
}

/* ---------------------------------------------------------- any -> any */

#define CHUNK 128

int vban_pcm_convert(const void *src, unsigned st, void *dst, unsigned dt, size_t n)
{
    float tmp[CHUNK];
    const uint8_t *s = (const uint8_t *)src;
    uint8_t *d = (uint8_t *)dst;
    size_t ss, ds;
    int r;
    if (!src || !dst) return VBAN_E_ARG;
    if (!valid_type(st) || !valid_type(dt)) return VBAN_E_UNSUPPORTED;
    if (st == dt) {
        if (src != dst) memmove(dst, src, n * vban_datatype_size[st]);
        return VBAN_OK;
    }
    ss = vban_datatype_size[st];
    ds = vban_datatype_size[dt];
    while (n) {
        size_t k = n < CHUNK ? n : CHUNK;
        if ((r = vban_pcm_to_f32(s, st, tmp, k)) != VBAN_OK) return r;
        if ((r = vban_pcm_from_f32(tmp, d, dt, k)) != VBAN_OK) return r;
        s += k * ss; d += k * ds; n -= k;
    }
    return VBAN_OK;
}

/* ---------------------------------------------------------- planar */

int vban_pcm_to_f32_planar(const void *src_, unsigned dt, unsigned ch, size_t frames, float *const *dst)
{
    const uint8_t *s = (const uint8_t *)src_;
    size_t ss, stride, f;
    unsigned c;
    if (!src_ || !dst || ch == 0) return VBAN_E_ARG;
    if (!valid_type(dt)) return VBAN_E_UNSUPPORTED;
    ss = vban_datatype_size[dt];
    stride = ss * ch;
    if (ch == 1) return dst[0] ? vban_pcm_to_f32(s, dt, dst[0], frames) : VBAN_OK;
    for (c = 0; c < ch; ++c) {
        float *o = dst[c];
        const uint8_t *p = s + c * ss;
        if (!o) continue;
        switch (dt) {
        case VBAN_DATATYPE_U8:  for (f = 0; f < frames; ++f) o[f] = ((int)p[f * stride] - 128) * (1.0f / 128.0f); break;
        case VBAN_DATATYPE_S16: for (f = 0; f < frames; ++f) o[f] = (int16_t)vban_rd16(p + f * stride) * (1.0f / 32768.0f); break;
        case VBAN_DATATYPE_S24: for (f = 0; f < frames; ++f) o[f] = ld_s24(p + f * stride) * (1.0f / 8388608.0f); break;
        case VBAN_DATATYPE_S32: for (f = 0; f < frames; ++f) o[f] = (float)(int32_t)vban_rd32(p + f * stride) * (1.0f / 2147483648.0f); break;
        case VBAN_DATATYPE_F32: for (f = 0; f < frames; ++f) o[f] = ld_f32_san(p + f * stride); break;
#if VBAN_ENABLE_F64
        case VBAN_DATATYPE_F64: for (f = 0; f < frames; ++f) o[f] = san((float)ld_f64(p + f * stride)); break;
#endif
        default: return VBAN_E_UNSUPPORTED;
        }
    }
    return VBAN_OK;
}

int vban_pcm_from_f32_planar(const float *const *src, unsigned ch, size_t frames, void *dst_, unsigned dt)
{
    uint8_t *d = (uint8_t *)dst_;
    size_t ss, stride, f;
    unsigned c;
    if (!src || !dst_ || ch == 0) return VBAN_E_ARG;
    if (!valid_type(dt)) return VBAN_E_UNSUPPORTED;
    ss = vban_datatype_size[dt];
    stride = ss * ch;
    for (c = 0; c < ch; ++c) {
        const float *in = src[c];
        uint8_t *p = d + c * ss;
        if (!in) {
            for (f = 0; f < frames; ++f) {
                if (dt == VBAN_DATATYPE_U8) p[f * stride] = 128;
                else memset(p + f * stride, 0, ss);
            }
            continue;
        }
        switch (dt) {
        case VBAN_DATATYPE_U8:  for (f = 0; f < frames; ++f) p[f * stride] = (uint8_t)(f2i(in[f], 128.0f, 127.0f) + 128); break;
        case VBAN_DATATYPE_S16: for (f = 0; f < frames; ++f) vban_wr16(p + f * stride, (uint16_t)(int16_t)f2i(in[f], 32768.0f, 32767.0f)); break;
        case VBAN_DATATYPE_S24: for (f = 0; f < frames; ++f) st_s24(p + f * stride, f2i_s24(in[f])); break;
        case VBAN_DATATYPE_S32: for (f = 0; f < frames; ++f) vban_wr32(p + f * stride, (uint32_t)f2i32(in[f])); break;
        case VBAN_DATATYPE_F32: for (f = 0; f < frames; ++f) st_f32(p + f * stride, in[f]); break;
#if VBAN_ENABLE_F64
        case VBAN_DATATYPE_F64: for (f = 0; f < frames; ++f) st_f64(p + f * stride, (double)in[f]); break;
#endif
        default: return VBAN_E_UNSUPPORTED;
        }
    }
    return VBAN_OK;
}

/* ---------------------------------------------------------- gain / peak */

int vban_pcm_gain(void *data, unsigned dt, size_t n, float g)
{
    uint8_t *d = (uint8_t *)data;
    size_t i;
    if (!data) return VBAN_E_ARG;
    if (g == 1.0f) return valid_type(dt) ? VBAN_OK : VBAN_E_UNSUPPORTED;
    switch (dt) {
    case VBAN_DATATYPE_U8:
        for (i = 0; i < n; ++i) d[i] = (uint8_t)(round_scl(((int)d[i] - 128) * g, -128.0f, 127.0f) + 128);
        return VBAN_OK;
    case VBAN_DATATYPE_S16:
        for (i = 0; i < n; ++i)
            vban_wr16(d + 2 * i, (uint16_t)(int16_t)round_scl((int16_t)vban_rd16(d + 2 * i) * g, -32768.0f, 32767.0f));
        return VBAN_OK;
    case VBAN_DATATYPE_S24:
        for (i = 0; i < n; ++i) {
            float v = ld_s24(d + 3 * i) * g;
            st_s24(d + 3 * i, round_scl(v, -8388608.0f, 8388607.0f));
        }
        return VBAN_OK;
    case VBAN_DATATYPE_S32:
        for (i = 0; i < n; ++i)
            vban_wr32(d + 4 * i, (uint32_t)f2i32((float)(int32_t)vban_rd32(d + 4 * i) * (1.0f / 2147483648.0f) * g));
        return VBAN_OK;
    case VBAN_DATATYPE_F32:
        for (i = 0; i < n; ++i) st_f32(d + 4 * i, ld_f32(d + 4 * i) * g);
        return VBAN_OK;
#if VBAN_ENABLE_F64
    case VBAN_DATATYPE_F64:
        for (i = 0; i < n; ++i) st_f64(d + 8 * i, ld_f64(d + 8 * i) * g);
        return VBAN_OK;
#endif
    default:
        return VBAN_E_UNSUPPORTED;
    }
}

float vban_pcm_peak(const void *src, unsigned dt, size_t n)
{
    float tmp[CHUNK], pk = 0.0f;
    const uint8_t *s = (const uint8_t *)src;
    size_t ss, i;
    if (!src || !valid_type(dt)) return (float)VBAN_E_UNSUPPORTED;
    ss = vban_datatype_size[dt];
    while (n) {
        size_t k = n < CHUNK ? n : CHUNK;
        vban_pcm_to_f32(s, dt, tmp, k);
        for (i = 0; i < k; ++i) {
            float a = fabsf(tmp[i]);
            pk = a > pk ? a : pk;
        }
        s += k * ss; n -= k;
    }
    return pk;
}

/* ---------------------------------------------------------- interleave */

void vban_f32_deinterleave(const float *in, float *const *out, unsigned ch, size_t frames)
{
    size_t f;
    unsigned c;
    if (ch == 2 && out[0] && out[1]) {
        float *l = out[0], *r = out[1];
        for (f = 0; f < frames; ++f) { l[f] = in[2 * f]; r[f] = in[2 * f + 1]; }
        return;
    }
    for (c = 0; c < ch; ++c)
        if (out[c])
            for (f = 0; f < frames; ++f) out[c][f] = in[f * ch + c];
}

void vban_f32_interleave(const float *const *in, float *out, unsigned ch, size_t frames)
{
    size_t f;
    unsigned c;
    for (c = 0; c < ch; ++c) {
        if (in[c]) for (f = 0; f < frames; ++f) out[f * ch + c] = in[c][f];
        else       for (f = 0; f < frames; ++f) out[f * ch + c] = 0.0f;
    }
}
