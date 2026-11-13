/*
 * netsim - deterministic network impairment simulator driving the real
 * jitter buffer in virtual time. Answers: on a given network, what latency
 * does the receiver settle at, and how many audible glitches are left?
 *
 *   vban_netsim [seconds] [fpp] [period]
 *
 * Model per scenario:
 *  - sender emits a packet every fpp frames on its own clock (+drift ppm)
 *  - delay = base + |gaussian jitter| + correlated spikes (a spike holds the
 *    FIFO queue, so following packets arrive bunched: Wi-Fi behaviour)
 *  - loss: Gilbert-Elliott two-state bursts
 *  - receiver pulls `period` frames on its own clock (an audio callback)
 * Reported: mean jitter-buffer delay at pull time, adaptive target reached,
 * underruns, concealed audio (loss), and the estimated mouth-to-ear latency
 * = packetization (fpp/2) + mean network delay + jitter buffer + period/2.
 */
#include "vban.h"
#include "../shared/vban_kring.h"   /* the snd-vban kernel ring (sotto-linux), compiled for userspace */
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct scenario {
    const char *name;
    double base_ms, jitter_ms;       /* constant + |N(0, jitter)| */
    double spike_prob, spike_ms;     /* probability per packet to start a spike, spike height */
    int    spike_len;                /* packets the spike decays over */
    double p_gb, p_bg;               /* Gilbert-Elliott good->bad, bad->good (bad = lost) */
    double drift_ppm;                /* receiver clock vs sender */
} scenario_t;

static const scenario_t k_scen[] = {
    { "wired LAN",        0.15, 0.03, 0.0,    0.0,  0, 0.0,    1.0,  30 },
    { "Wi-Fi good",       1.5,  0.6,  0.004,  12.0, 6, 0.002,  0.5,  -40 },
    { "Wi-Fi congested",  3.0,  2.0,  0.015,  40.0, 12, 0.01,  0.35, 60 },
    { "WAN / VPN",        25.0, 3.0,  0.003,  30.0, 8, 0.004,  0.5,  -80 },
};

/* --- deterministic PRNG --- */
static uint64_t g_rng = 0x9E3779B97F4A7C15ull;
static double urand(void)
{
    g_rng ^= g_rng << 13; g_rng ^= g_rng >> 7; g_rng ^= g_rng << 17;
    return (double)(g_rng >> 11) * (1.0 / 9007199254740992.0);
}
static double gauss(void)
{
    double u = urand() + 1e-12, v = urand();
    return sqrt(-2 * log(u)) * cos(6.283185307179586 * v);
}

typedef struct arrival { double t; uint32_t frame; } arrival_t;

/* FEC under test: 0 = none, 1 = duplicate each packet D slots later,
 * 2 = XOR parity over K packets (repairs one loss per group). */
static int g_fec_mode, g_fec_arg;

/* Receiver engine: 0 = SDK jitter buffer (float, adaptive), >0 = the snd-vban
 * kernel ring (integer, fixed target of this many frames). */
static int g_kring_target;
static int cmp_arr(const void *a, const void *b)
{
    double x = ((const arrival_t *)a)->t, y = ((const arrival_t *)b)->t;
    return x < y ? -1 : x > y;
}

static int cmp_dbl(const void *a, const void *b)
{
    double x = *(const double *)a, y = *(const double *)b;
    return x < y ? -1 : x > y;
}

typedef struct result {
    double jb_ms, net_ms, e2e_ms, need999_ms, need99_ms;
    double glitch_ms_min;   /* audible non-stream audio per minute: silence (priming/underrun) + concealment */
    uint32_t target, underruns, lost_pkts, late, concealed_frames, skips, repaired;
    uint64_t kr_reprimes, kr_skips;   /* kernel engine only */
} result_t;

static result_t run(const scenario_t *sc, double seconds, unsigned fpp, unsigned period, uint32_t min_frames)
{
    enum { RATE = 48000, CH = 2, CAP = 16384 };
    static float ring[CAP * CH];
    static float out[1024 * CH];
    static uint8_t pkt[VBAN_PACKET_MAX_SIZE];
    vban_jb_t jb;
    vban_jb_config_t jc;
    vban_jb_stats_t st;
    vban_name_t name;
    result_t r;
    unsigned npk = (unsigned)(seconds * RATE / fpp), i, ai = 0, narr = 0;
    arrival_t *arr = (arrival_t *)malloc(sizeof(arrival_t) * npk);
    double last_arr = 0, spike = 0, net_sum = 0, jb_sum = 0, pull_t, pull_dt;
    int bad = 0, spike_left = 0;
    unsigned long pulls = 0;
    double glitch_frames = 0;
    uint32_t conc0 = 0;
    int measuring = 0;

    memset(&jc, 0, sizeof jc);
    jc.channels = CH; jc.adaptive = 1; jc.drift = VBAN_JB_DRIFT_RESAMPLE;
    jc.target_frames = 2 * fpp; jc.min_frames = min_frames;
    vban_jb_init(&jb, &jc, ring, CAP);
    static s16 kring_buf[KRING_FRAMES * CH];
    static int16_t out16[1024 * CH];
    struct vban_kring kr;
    kring_init(&kr, kring_buf, CH, (int)fpp, g_kring_target > 0 ? g_kring_target : 256);
    kring_start(&kr);
    vban_name_set(&name, "sim");

    /* ---- network: arrival times, FIFO, loss ---- */
    {
        /* per transmission slot: lost? and arrival time (copies / parity share the slot of the
         * data packet they are sent with: same channel state, same queue) */
        double *slot_t = (double *)malloc(sizeof(double) * npk);
        unsigned char *slot_lost = (unsigned char *)malloc(npk);
        unsigned cap_arr = npk * 2 + 16;
        arr = (arrival_t *)realloc(arr, sizeof(arrival_t) * cap_arr);
        for (i = 0; i < npk; ++i) {
            double send_t = (double)i * fpp / RATE, d, a;
            bad = bad ? (urand() >= sc->p_bg) : (urand() < sc->p_gb);
            if (spike_left == 0 && urand() < sc->spike_prob) { spike_left = sc->spike_len; spike = sc->spike_ms; }
            d = sc->base_ms + fabs(gauss()) * sc->jitter_ms;
            if (spike_left) { d += spike * spike_left / sc->spike_len; --spike_left; }
            a = send_t + d / 1000.0;
            if (a < last_arr) a = last_arr + 1e-6;  /* FIFO: queued packets leave in order */
            last_arr = a;
            slot_t[i] = a;
            slot_lost[i] = (unsigned char)bad;
            if (bad) continue;
            net_sum += a - send_t;
            arr[narr].t = a; arr[narr].frame = i; ++narr;
        }
        r.lost_pkts = npk - narr;
        if (g_fec_mode == 1) {                 /* copy of packet i rides in slot i + D */
            unsigned D = (unsigned)g_fec_arg;
            for (i = 0; i + D < npk; ++i)
                if (!slot_lost[i + D]) { arr[narr].t = slot_t[i + D] + 1e-7; arr[narr].frame = i; ++narr; }
        } else if (g_fec_mode == 2) {          /* parity of group g sent right after its last packet */
            unsigned K = (unsigned)g_fec_arg, g0;
            for (g0 = 0; g0 + K <= npk; g0 += K) {
                unsigned miss = 0, which = 0, k;
                double tp = slot_t[g0 + K - 1] + 1e-7, ready = tp;
                if (slot_lost[g0 + K - 1]) continue;           /* parity lost with its slot */
                for (k = 0; k < K; ++k) if (slot_lost[g0 + k]) { ++miss; which = g0 + k; }
                if (miss != 1) continue;
                for (k = 0; k < K; ++k) if (!slot_lost[g0 + k] && slot_t[g0 + k] > ready) ready = slot_t[g0 + k];
                arr[narr].t = ready; arr[narr].frame = which; ++narr;  /* rebuilt when all parts are in */
            }
        }
        free(slot_t);
        free(slot_lost);
    }
    qsort(arr, narr, sizeof arr[0], cmp_arr);
    { /* oracle: buffer needed so 99% / 99.9% of packets are on time = quantile(delay) - min(delay) */
        double *dl = (double *)malloc(sizeof(double) * (narr ? narr : 1));
        unsigned k;
        for (k = 0; k < narr; ++k) dl[k] = arr[k].t - (double)arr[k].frame * fpp / RATE;
        qsort(dl, narr, sizeof dl[0], cmp_dbl);
        r.need99_ms = narr ? (dl[narr * 99 / 100] - dl[0]) * 1000 : 0;
        r.need999_ms = narr ? (dl[narr * 999 / 1000] - dl[0]) * 1000 : 0;
        free(dl);
    }

    /* ---- receiver: audio callback on its own (drifting) clock ---- */
    pull_dt = (double)period / RATE * (1.0 + sc->drift_ppm * 1e-6);
    for (pull_t = 0.2; pull_t < seconds; pull_t += pull_dt) {
        vban_packet_t p;
        while (ai < narr && arr[ai].t <= pull_t) {
            vban_build_audio_header(pkt, &name, RATE, fpp, CH, VBAN_DATATYPE_S16, arr[ai].frame);
            memset(pkt + 28, 0, fpp * CH * 2);
            vban_parse(pkt, 28 + fpp * CH * 2, &p, 0);
            if (g_kring_target) kring_place(&kr, &p);
            else vban_jb_push(&jb, &p);
            ++ai;
        }
        if (pull_t > 5.0) { /* skip the convergence period */
            vban_jb_get_stats(&jb, &st);
            if (g_kring_target) st.concealed_frames = (uint32_t)kr.concealed;
            jb_sum += (double)(g_kring_target ? (uint32_t)(kring_fill(&kr) > 0 ? kring_fill(&kr) : 0) : st.fill_frames) / RATE;
            ++pulls;
            if (!measuring) { measuring = 1; conc0 = st.concealed_frames; }
        }
        {
            size_t got = g_kring_target ? kring_pull(&kr, (__le16 *)out16, period)
                                        : vban_jb_pull_f32(&jb, out, period);
            if (measuring) glitch_frames += (double)(period - got);
            if (getenv("NETSIM_DEBUG") && ((unsigned long)(pull_t * 1000) % 5000) < 3) {
                vban_jb_stats_t d;
                vban_jb_get_stats(&jb, &d);
                fprintf(stderr, "t %.2f got %zu fill %u tgt %u under %u late %u dup %u rep %u rej %u resets %u ovr %u play %d stall %d conc %u\n",
                        pull_t, got, d.fill_frames, d.target_frames, d.underruns, d.late, d.dup, d.repaired,
                        d.rejected, d.resets, d.overruns, jb.playing, jb.stalled, d.concealed_frames);
            }
        }
    }
    vban_jb_get_stats(&jb, &st);
    if (g_kring_target) {
        st.concealed_frames = (uint32_t)kr.concealed;
        st.target_frames = (uint32_t)kr.target;
        st.underruns = (uint32_t)kr.underruns;
        st.repaired = (uint32_t)kr.repaired;
        st.late = (uint32_t)kr.late;
        st.skips = (uint32_t)kr.skips;
    }
    glitch_frames += (double)(st.concealed_frames - conc0);
    r.glitch_ms_min = glitch_frames / RATE * 1000.0 / ((seconds - 5.0) / 60.0);
    r.jb_ms = pulls ? jb_sum / pulls * 1000 : 0;
    r.net_ms = net_sum / (double)(npk - r.lost_pkts) * 1000;
    r.e2e_ms = (double)fpp / 2 / RATE * 1000 + r.net_ms + r.jb_ms + (double)period / 2 / RATE * 1000;
    r.target = st.target_frames;
    r.underruns = st.underruns;
    r.repaired = st.repaired;
    r.late = st.late;
    r.concealed_frames = st.concealed_frames;
    r.skips = st.skips;
    r.kr_reprimes = g_kring_target ? kr.reprimes : 0;
    r.kr_skips = g_kring_target ? kr.skips : 0;
    if (g_kring_target && getenv("NETSIM_KRING_STATS"))
        fprintf(stderr, "  kring: concealed loss %llu stall %llu, re-primes %llu, skips %llu, ratio %+d ppm\n",
                (unsigned long long)kr.concealed_loss, (unsigned long long)kr.concealed_stall,
                (unsigned long long)kr.reprimes, (unsigned long long)kr.skips,
                (int)(((int64_t)kr.ratio_q16 - 65536) * 1000000 / 65536));
    free(arr);
    return r;
}

#ifndef NETSIM_NO_MAIN
int main(int argc, char **argv)
{
    double secs = argc > 1 ? atof(argv[1]) : 120;
    unsigned fpp = argc > 2 ? (unsigned)atoi(argv[2]) : 64;
    unsigned period = argc > 3 ? (unsigned)atoi(argv[3]) : 128;
    const char *mode = argc > 4 ? argv[4] : "all";
    const char *fec = argc > 6 ? argv[6] : "none";
    if (!strncmp(fec, "dup:", 4)) { g_fec_mode = 1; g_fec_arg = atoi(fec + 4); }
    else if (!strncmp(fec, "xor:", 4)) { g_fec_mode = 2; g_fec_arg = atoi(fec + 4); }
    unsigned s;
    printf("mode: %s  fec: %s\n", mode, fec);
    printf("netsim: %.0f s per scenario, 48 kHz stereo, %u frames/packet, %u-frame audio period\n\n", secs, fpp, period);
    printf("%-24s %8s %8s %8s %8s %9s %9s %9s %9s %6s %9s %11s\n", "scenario", "net ms", "jb ms", "E2E ms", "target", "underrun", "lost pkt", "repaired", "late pkt", "skips", "need p999", "glitch ms/m");
    {
    const char *engine = argc > 7 ? argv[7] : "sdk";   /* sdk | kring:<frames> | both */
    for (s = 0; s < sizeof k_scen / sizeof k_scen[0]; ++s) {
        result_t r;
        int pass, passes = !strcmp(engine, "both") ? 2 : 1;
        uint32_t sdk_target = 0;
        for (pass = 0; pass < passes; ++pass) {
            const char *tag = "";
            g_rng = 0x9E3779B97F4A7C15ull + s;   /* identical network for both engines */
            scenario_t sc = k_scen[s];
            if (!strcmp(mode, "noloss")) sc.p_gb = 0;
            if (!strcmp(mode, "nojitter")) { sc.jitter_ms = 0; sc.spike_prob = 0; }
            if (!strcmp(mode, "nodrift")) sc.drift_ppm = 0;
            if (!strncmp(engine, "kring:", 6)) { g_kring_target = atoi(engine + 6); tag = " [kernel]"; }
            else if (pass == 1) { g_kring_target = (int)sdk_target; tag = " [kernel]"; }  /* same buffering as the SDK reached */
            else { g_kring_target = 0; tag = passes == 2 ? " [sdk]" : ""; }
            r = run(&sc, secs, fpp, period, argc > 5 ? (uint32_t)atoi(argv[5]) : 64);
            if (pass == 0) sdk_target = r.target;
            {
                char label[48];
                snprintf(label, sizeof label, "%s%s", k_scen[s].name, tag);
                printf("%-24s %8.2f %8.2f %8.2f %8u %9u %9u %9u %9u %6u %9.2f %11.1f\n", label, r.net_ms, r.jb_ms, r.e2e_ms,
                       r.target, r.underruns, r.lost_pkts, r.repaired, r.late, r.skips, r.need999_ms, r.glitch_ms_min);
            }
        }
    }
    }
    return 0;
}
#endif /* NETSIM_NO_MAIN */
