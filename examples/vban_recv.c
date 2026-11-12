/*
 * vban_recv - receive one VBAN audio stream and write raw PCM to stdout.
 *
 *   vban_recv -n Stream1 | aplay -f S16_LE -r 48000 -c 2
 *   vban_recv -n Stream1 -s 192.168.1.20 -o f32 > capture.f32
 *
 *   -n name   stream name (Stream1)       -s ip    only accept this source
 *   -p port   listen port (6980)          -c ch    output channels (2)
 *   -r rate   output rate (0 = stream rate, resampled if different)
 *   -o fmt    s16 | f32 (s16)             -l ms    initial latency target (10)
 *   --slip    drift by frame slip instead of resampling
 *   -q        quiet (no stats on stderr)
 *
 * The network side runs on a thread-free poll loop; output is clocked by the
 * wall clock at the output rate, standing in for an audio device callback.
 */
#include "common.h"

static vban_jb_t g_jb;

int main(int argc, char **argv)
{
    vban_socket_t s = VBAN_SOCKET_INIT;
    vban_rx_t rx;
    vban_jb_config_t jc;
    vban_addr_t src;
    static uint8_t bufs[VBAN_NET_BATCH][VBAN_PACKET_MAX_SIZE];
    vban_msg_t msgs[VBAN_NET_BATCH];
    static float ring[16384 * 8], out[256 * 8];
    static int16_t out16[256 * 8];
    unsigned ch = (unsigned)arg_int(argc, argv, "-c", 2), i;
    uint32_t out_rate = (uint32_t)arg_int(argc, argv, "-r", 0);
    int as_f32 = strcmp(arg_str(argc, argv, "-o", "s16"), "f32") == 0, quiet = arg_flag(argc, argv, "-q");
    const char *srcip = arg_str(argc, argv, "-s", NULL);
    uint64_t next_out = 0, next_stat;
    const unsigned period = 128;

    ex_init();
    if (ch < 1 || ch > 8) { fprintf(stderr, "1..8 output channels\n"); return 2; }
    memset(&jc, 0, sizeof jc);
    jc.channels = (uint16_t)ch;
    jc.out_rate = out_rate;
    jc.adaptive = 1;
    jc.drift = arg_flag(argc, argv, "--slip") ? VBAN_JB_DRIFT_SLIP : VBAN_JB_DRIFT_RESAMPLE;
    jc.target_frames = (uint32_t)(arg_int(argc, argv, "-l", 10) * 48);
    vban_jb_init(&g_jb, &jc, ring, 16384);

    vban_rx_init(&rx, NULL);
    src.ip = 0;
    if (srcip && vban_addr_parse(srcip, 0, &src) != VBAN_OK) { fprintf(stderr, "bad source ip\n"); return 2; }
    vban_rx_subscribe_audio(&rx, arg_str(argc, argv, "-n", "Stream1"), src.ip, &g_jb, NULL, NULL);

    if (vban_udp_open(&s, NULL, (uint16_t)arg_int(argc, argv, "-p", VBAN_DEFAULT_PORT), VBAN_SOCK_REUSE) != VBAN_OK) {
        fprintf(stderr, "cannot bind port\n");
        return 1;
    }
    vban_udp_set_buffers(&s, 1 << 20, 0);
    vban_thread_realtime();
    for (i = 0; i < VBAN_NET_BATCH; ++i) { msgs[i].buf = bufs[i]; msgs[i].cap = sizeof bufs[i]; }
    next_stat = vban_time_ns() + 1000000000ull;

    while (!g_stop) {
        uint64_t now = vban_time_ns();
        long wait_us = next_out ? (long)((next_out > now ? next_out - now : 0) / 1000) : 100000;
        int n = vban_udp_recv_batch_us(&s, msgs, VBAN_NET_BATCH, wait_us);
        for (i = 0; n > 0 && i < (unsigned)n; ++i) vban_rx_process(&rx, msgs[i].buf, msgs[i].len, &msgs[i].addr, now);

        now = vban_time_ns();
        if (!next_out && g_jb.in_rate) next_out = now;
        while (next_out && now >= next_out) {   /* "audio callback" */
            uint32_t rate = out_rate ? out_rate : g_jb.in_rate;
            if (as_f32) {
                vban_jb_pull_f32(&g_jb, out, period);
                fwrite(out, sizeof(float) * ch, period, stdout);
            } else {
                vban_jb_pull_s16(&g_jb, out16, period);
                fwrite(out16, sizeof(int16_t) * ch, period, stdout);
            }
            next_out += (uint64_t)period * 1000000000ull / (rate ? rate : 48000);
        }
        if (!quiet && now >= next_stat) {
            vban_jb_stats_t st;
            vban_jb_get_stats(&g_jb, &st);
            fprintf(stderr, "\r%u Hz pkts %u lost %u repaired %u late %u under %u latency %.1f ms target %u ratio %.5f   ",
                    st.stream_rate, st.packets, st.lost, st.repaired, st.late, st.underruns, vban_jb_latency_us(&g_jb) / 1000.0,
                    st.target_frames, st.ratio);
            next_stat = now + 1000000000ull;
        }
    }
    vban_udp_close(&s);
    vban_net_cleanup();
    return 0;
}
