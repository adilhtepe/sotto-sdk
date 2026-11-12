/*
 * VBAN SDK benchmark: CPU cost of every hot path, end-to-end UDP loopback
 * latency percentiles, and RAM footprint of every object.
 *
 *   vban_bench [quick]
 */
#include "vban.h"
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static volatile uint32_t g_sink;
static int null_send(void *c, const uint8_t *p, size_t n, const vban_addr_t *t)
{ (void)c; (void)t; g_sink += p[n - 1]; return (int)n; }

#define BENCH(label, iters, unit_div, unit, ...) do {                         \
    long _i; uint64_t _t0, _t1; double _ns;                                     \
    for (_i = 0; _i < (iters) / 10 + 1; ++_i) { __VA_ARGS__; }                         \
    _t0 = vban_time_ns();                                                       \
    for (_i = 0; _i < (iters); ++_i) { __VA_ARGS__; }                                  \
    _t1 = vban_time_ns();                                                       \
    _ns = (double)(_t1 - _t0) / (double)(iters);                                \
    printf("  %-44s %10.1f ns/op  %8.2f ns/%s\n", label, _ns, _ns / (unit_div), unit); \
} while (0)

static int cmp_u64(const void *a, const void *b)
{
    uint64_t x = *(const uint64_t *)a, y = *(const uint64_t *)b;
    return x < y ? -1 : x > y;
}

int main(int argc, char **argv)
{
    long N = (argc > 1 && strcmp(argv[1], "quick") == 0) ? 20000 : 200000;
    static uint8_t pkt[VBAN_PACKET_MAX_SIZE], wire[256 * 8 * 8];
    static float f[256 * 8], f2[256 * 8], ring[8192 * 2];
    static int16_t s16[256 * 8];
    vban_name_t name;
    vban_packet_t p;
    int i;

    printf("VBAN SDK %s benchmark (%ld iterations)\n\n", VBAN_SDK_VERSION, N);
    vban_name_set(&name, "Stream1");
    for (i = 0; i < 256 * 8; ++i) f[i] = (float)sin(i * 0.01) * 0.8f;

    /* ---------------- protocol */
    printf("protocol\n");
    vban_build_audio_header(pkt, &name, 48000, 256, 2, VBAN_DATATYPE_S16, 0);
    vban_pcm_from_f32(f, pkt + 28, VBAN_DATATYPE_S16, 512);
    BENCH("vban_parse (256x2 s16)", N * 10, 1, "pkt", { vban_parse(pkt, 28 + 1024, &p, 0); g_sink += p.samples; });
    {
        vban_rx_t rx;
        vban_rx_init(&rx, NULL);
        for (i = 0; i < VBAN_RX_MAX_STREAMS; ++i) {
            char nm[16];
            snprintf(nm, sizeof nm, "Other%d", i);
            vban_rx_subscribe(&rx, i == VBAN_RX_MAX_STREAMS - 1 ? "Stream1" : nm, 0, VBAN_PROTOCOL_AUDIO, NULL, NULL);
        }
        BENCH("vban_rx_process, 8 subs, match last", N * 10, 1, "pkt",
              { vban_addr_t a = { 1, 1 }; g_sink += (uint32_t)vban_rx_process(&rx, pkt, 28 + 1024, &a, 0); });
    }

    /* ---------------- PCM */
    printf("\npcm conversion (512 samples = 256 frames x 2 ch)\n");
    BENCH("s16 -> f32", N, 512, "sample", { vban_pcm_to_f32(pkt + 28, VBAN_DATATYPE_S16, f2, 512); });
    BENCH("f32 -> s16", N, 512, "sample", { vban_pcm_from_f32(f, wire, VBAN_DATATYPE_S16, 512); });
    vban_pcm_from_f32(f, wire, VBAN_DATATYPE_S24, 512);
    BENCH("s24 -> f32", N, 512, "sample", { vban_pcm_to_f32(wire, VBAN_DATATYPE_S24, f2, 512); });
    BENCH("f32 -> s24", N, 512, "sample", { vban_pcm_from_f32(f, wire, VBAN_DATATYPE_S24, 512); });
    BENCH("f32 -> f32 (wire)", N, 512, "sample", { vban_pcm_from_f32(f, wire, VBAN_DATATYPE_F32, 512); });
    vban_pcm_from_f32(f, wire, VBAN_DATATYPE_F32, 512);
    BENCH("f32 (wire) -> f32", N, 512, "sample", { vban_pcm_to_f32(wire, VBAN_DATATYPE_F32, f2, 512); });
    BENCH("f32 (wire) -> s16", N, 512, "sample", { vban_pcm_to_s16(wire, VBAN_DATATYPE_F32, s16, 512); });
    BENCH("s16 -> s16 native", N, 512, "sample", { vban_pcm_to_s16(pkt + 28, VBAN_DATATYPE_S16, s16, 512); });
    vban_pcm_from_f32(f, wire, VBAN_DATATYPE_S16, 512);
    BENCH("gain in place s16", N, 512, "sample", { vban_pcm_gain(wire, VBAN_DATATYPE_S16, 512, 0.7f); });

    /* ---------------- tx */
    printf("\ntransmit\n");
    {
        vban_tx_t tx;
        vban_tx_config_t c;
        vban_addr_t d = { 1, 1 };
        memset(&c, 0, sizeof c);
        c.name = "Stream1"; c.sample_rate = 48000; c.channels = 2; c.datatype = VBAN_DATATYPE_S16;
        c.frames_per_packet = 256; c.send = null_send;
        vban_tx_init(&tx, &c);
        vban_tx_add_dest(&tx, &d);
        BENCH("tx write_f32 256 frames -> 1 pkt (s16)", N, 1, "pkt", { vban_tx_write_f32(&tx, f, 256); });
        c.frames_per_packet = 32;
        vban_tx_init(&tx, &c);
        vban_tx_add_dest(&tx, &d);
        BENCH("tx write_f32 256 frames -> 8 pkts of 32", N, 8, "pkt", { vban_tx_write_f32(&tx, f, 256); });
    }

    /* ---------------- jitter buffer */
    printf("\njitter buffer (stereo)\n");
    {
        vban_jb_t jb;
        vban_jb_config_t jc;
        static uint8_t pk[VBAN_PACKET_MAX_SIZE];
        uint32_t fr = 0;
        memset(&jc, 0, sizeof jc);
        jc.channels = 2; jc.target_frames = 512; jc.drift = VBAN_JB_DRIFT_NONE;
        vban_jb_init(&jb, &jc, ring, 8192);
        memcpy(pk, pkt, 28 + 1024);
        BENCH("push 256 + pull 256 (no drift ctl)", N, 256, "frame", {
            vban_header_set_frame(pk, fr++); vban_parse(pk, 28 + 1024, &p, 0);
            vban_jb_push(&jb, &p); vban_jb_pull_f32(&jb, f2, 256); });
        jc.drift = VBAN_JB_DRIFT_RESAMPLE;
        vban_jb_init(&jb, &jc, ring, 8192);
        BENCH("push 256 + pull 256 (resample)", N, 256, "frame", {
            vban_header_set_frame(pk, fr++); vban_parse(pk, 28 + 1024, &p, 0);
            vban_jb_push(&jb, &p); vban_jb_pull_f32(&jb, f2, 256); });
        jc.drift = VBAN_JB_DRIFT_NONE; jc.storage = VBAN_JB_S16;
        vban_jb_init(&jb, &jc, ring, 8192);
        BENCH("push 256 + pull_s16 256 (s16 ring)", N, 256, "frame", {
            vban_header_set_frame(pk, fr++); vban_parse(pk, 28 + 1024, &p, 0);
            vban_jb_push(&jb, &p); vban_jb_pull_s16(&jb, s16, 256); });
    }

    /* ---------------- mixer */
    printf("\nmixer\n");
    {
        static uint32_t mem[1024];
        vban_mixer_t mx;
        static float bufs[16][128];
        const float *in[8];
        float *out[8];
        unsigned a, b;
        vban_mixer_init(&mx, mem, sizeof mem, 8, 8);
        for (a = 0; a < 8; ++a) { in[a] = bufs[a]; out[a] = bufs[8 + a]; for (b = 0; b < 8; ++b) vban_mixer_set_route(&mx, a, b, 0.1f); }
        BENCH("8x8 full matrix, 128 frames", N, 128 * 64, "mac", { vban_mixer_process(&mx, in, out, 128); });
        vban_mixer_clear_routes(&mx);
        for (a = 0; a < 8; ++a) vban_mixer_set_route(&mx, a, a, 1.0f);
        BENCH("8x8 diagonal (sparse), 128 frames", N, 128 * 8, "mac", { vban_mixer_process(&mx, in, out, 128); });
    }

    /* ---------------- end to end over loopback UDP */
    printf("\nend-to-end: tx_write -> UDP loopback -> recv -> rx_process -> jb_push\n");
    {
        vban_socket_t rs = VBAN_SOCKET_INIT, ts = VBAN_SOCKET_INIT;
        vban_tx_t tx;
        vban_tx_config_t c;
        vban_rx_t rx;
        vban_jb_t jb;
        vban_jb_config_t jc;
        vban_addr_t to, from;
        static uint8_t rb[VBAN_PACKET_MAX_SIZE];
        long M = N / 10 > 20000 ? 20000 : N / 10, k;
        uint64_t *lat = (uint64_t *)malloc(sizeof(uint64_t) * (size_t)M);
        vban_net_init();
        vban_udp_open(&rs, "127.0.0.1", 0, 0);
        vban_udp_open(&ts, NULL, 0, VBAN_SOCK_QOS);
        vban_addr_parse("127.0.0.1", (uint16_t)vban_udp_local_port(&rs), &to);
        memset(&c, 0, sizeof c);
        c.name = "Stream1"; c.sample_rate = 48000; c.channels = 2; c.datatype = VBAN_DATATYPE_S16;
        c.frames_per_packet = 64; c.send = vban_udp_sink; c.send_ctx = &ts;
        vban_tx_init(&tx, &c);
        vban_tx_add_dest(&tx, &to);
        memset(&jc, 0, sizeof jc);
        jc.channels = 2; jc.drift = VBAN_JB_DRIFT_NONE;
        vban_jb_init(&jb, &jc, ring, 8192);
        vban_rx_init(&rx, NULL);
        vban_rx_subscribe_audio(&rx, "Stream1", 0, &jb, NULL, NULL);
        for (k = 0; k < M; ++k) {
            uint64_t t0 = vban_time_ns();
            int n;
            vban_tx_write_f32(&tx, f, 64);
            n = vban_udp_recv(&rs, rb, sizeof rb, &from, 1000);
            if (n > 0) vban_rx_process(&rx, rb, (size_t)n, &from, 0);
            lat[k] = vban_time_ns() - t0;
            if ((k & 63) == 0) vban_jb_pull_f32(&jb, f2, 256), vban_jb_pull_f32(&jb, f2, 256);
            if (jb.w - jb.r > 4096) vban_jb_request_reset(&jb), vban_jb_pull_f32(&jb, f2, 1);
        }
        qsort(lat, (size_t)M, sizeof lat[0], cmp_u64);
        printf("  packets %ld  p50 %.1f us  p99 %.1f us  p99.9 %.1f us  max %.1f us\n", M,
               lat[M / 2] / 1e3, lat[M * 99 / 100] / 1e3, lat[M * 999 / 1000] / 1e3, lat[M - 1] / 1e3);
        printf("  (+ audio buffering: 64 frames @48k = 1.33 ms per packet, jitter target as configured)\n");
        free(lat);
        vban_udp_close(&rs);
        vban_udp_close(&ts);
        vban_net_cleanup();
    }

    /* ---------------- memory */
    printf("\nRAM footprint (no heap is ever used by the SDK)\n");
    printf("  vban_packet_t (view)        %5zu B\n", sizeof(vban_packet_t));
    printf("  vban_jb_t                   %5zu B  + ring: frames*ch*4 (f32) or *2 (s16)\n", sizeof(vban_jb_t));
    printf("      e.g. 2048 frames stereo s16 ring = %zu B (42 ms @48k)\n", vban_jb_storage_bytes(2048, 2, VBAN_JB_S16));
    printf("  vban_mixer_t                %5zu B  + matrix: 8x8 = %zu B\n", sizeof(vban_mixer_t), vban_mixer_mem_size(8, 8));
    printf("  vban_tx_t                   %5zu B  (holds one 1464 B packet)\n", sizeof(vban_tx_t));
    printf("  vban_rx_t (%2d subs)         %5zu B\n", VBAN_RX_MAX_STREAMS, sizeof(vban_rx_t));
    printf("  vban_out_t                  %5zu B\n", sizeof(vban_out_t));
    printf("  vban_reasm_t                %5zu B  + caller buffer\n", sizeof(vban_reasm_t));
    printf("  vban_ping0_t (decoded)      %5zu B\n", sizeof(vban_ping0_t));
    return (int)(g_sink & 0);
}
