/*
 * vban_mixer_demo - a tiny network mixer: receive two stereo VBAN streams,
 * mix them with gains and send the stereo result as a new stream. It also
 * answers PING0 as a virtual mixer so it shows up in VBAN device lists.
 *
 *   vban_mixer_demo -a StreamA -b StreamB -o 192.168.1.10 -n Mix -ga 0.8 -gb 0.5
 *
 *   -p port   listen port (6980)   -o ip    destination (127.0.0.1)
 *   -op port  destination port (6980)      -n name  output stream (Mix)
 *   -r rate   mix rate (48000)     -b64     period 64 frames (default 128)
 */
#include "common.h"

int main(int argc, char **argv)
{
    enum { CH = 2, MAXP = 256 };
    vban_socket_t s = VBAN_SOCKET_INIT;
    vban_rx_t rx;
    vban_rx_config_t rc;
    vban_ping0_t me;
    static float ring_a[8192 * CH], ring_b[8192 * CH];
    vban_jb_t jb_a, jb_b;
    vban_jb_config_t jc;
    vban_mixer_t mx;
    static uint32_t mx_mem[64];
    vban_tx_t tx;
    vban_tx_config_t tc;
    vban_addr_t to;
    static uint8_t bufs[VBAN_NET_BATCH][VBAN_PACKET_MAX_SIZE];
    vban_msg_t msgs[VBAN_NET_BATCH];
    static float in_a[MAXP * CH], in_b[MAXP * CH], la[MAXP], ra[MAXP], lb[MAXP], rb[MAXP], ol[MAXP], or_[MAXP];
    const float *ins[4] = { la, ra, lb, rb };
    float *outs[2] = { ol, or_ };
    float ga = (float)atof(arg_str(argc, argv, "-ga", "1")), gb = (float)atof(arg_str(argc, argv, "-gb", "1"));
    uint32_t rate = (uint32_t)arg_int(argc, argv, "-r", 48000);
    unsigned period = arg_flag(argc, argv, "-b64") ? 64 : 128, i;
    uint64_t next, stat;

    ex_init();
    memset(&jc, 0, sizeof jc);
    jc.channels = CH; jc.out_rate = rate; jc.adaptive = 1; jc.drift = VBAN_JB_DRIFT_RESAMPLE;
    jc.target_frames = 256;
    vban_jb_init(&jb_a, &jc, ring_a, 8192);
    vban_jb_init(&jb_b, &jc, ring_b, 8192);

    vban_mixer_init(&mx, mx_mem, sizeof mx_mem, 4, 2);
    vban_mixer_clear_routes(&mx);
    vban_mixer_set_route(&mx, 0, 0, ga); vban_mixer_set_route(&mx, 1, 1, ga);
    vban_mixer_set_route(&mx, 2, 0, gb); vban_mixer_set_route(&mx, 3, 1, gb);
    vban_mixer_set_limiter(&mx, VBAN_LIMIT_SOFT);

    memset(&me, 0, sizeof me);
    me.bit_type = VBAN_PING_TYPE_VIRTUALMIXER | VBAN_PING_TYPE_RECEPTORSPOT | VBAN_PING_TYPE_TRANSMITTER;
    me.bit_feature = VBAN_PING_FEATURE_AUDIO;
    me.prefered_rate = rate; me.min_rate = 6000; me.max_rate = 705600;
    strcpy(me.device_name, "VBAN SDK mixer");
    strcpy(me.application_name, "vban_mixer_demo");

    if (vban_udp_open(&s, NULL, (uint16_t)arg_int(argc, argv, "-p", 6980), VBAN_SOCK_REUSE | VBAN_SOCK_QOS | VBAN_SOCK_BROADCAST) ||
        vban_addr_parse(arg_str(argc, argv, "-o", "127.0.0.1"), (uint16_t)arg_int(argc, argv, "-op", 6980), &to)) {
        fprintf(stderr, "socket / address error\n");
        return 1;
    }
    memset(&rc, 0, sizeof rc);
    rc.identity = &me; rc.send = vban_udp_sink; rc.send_ctx = &s;
    vban_rx_init(&rx, &rc);
    vban_rx_subscribe_audio(&rx, arg_str(argc, argv, "-a", "StreamA"), 0, &jb_a, NULL, NULL);
    vban_rx_subscribe_audio(&rx, arg_str(argc, argv, "-b", "StreamB"), 0, &jb_b, NULL, NULL);

    memset(&tc, 0, sizeof tc);
    tc.name = arg_str(argc, argv, "-n", "Mix");
    tc.sample_rate = rate; tc.channels = CH; tc.datatype = VBAN_DATATYPE_S16;
    tc.frames_per_packet = (uint16_t)period; tc.send = vban_udp_sink; tc.send_ctx = &s;
    if (vban_tx_init(&tx, &tc)) { fprintf(stderr, "bad rate\n"); return 2; }
    vban_tx_add_dest(&tx, &to);

    for (i = 0; i < VBAN_NET_BATCH; ++i) { msgs[i].buf = bufs[i]; msgs[i].cap = sizeof bufs[i]; }
    vban_thread_realtime();
    next = vban_time_ns();
    stat = next + 1000000000ull;
    fprintf(stderr, "mixing '%s' + '%s' -> '%s', period %u frames (%.2f ms)\n", arg_str(argc, argv, "-a", "StreamA"),
            arg_str(argc, argv, "-b", "StreamB"), tc.name, period, period * 1000.0 / rate);

    while (!g_stop) {
        uint64_t now = vban_time_ns();
        int n = vban_udp_recv_batch_us(&s, msgs, VBAN_NET_BATCH, next > now ? (long)((next - now) / 1000) : 0);
        for (i = 0; n > 0 && i < (unsigned)n; ++i) vban_rx_process(&rx, msgs[i].buf, msgs[i].len, &msgs[i].addr, now);
        now = vban_time_ns();
        while (now >= next) {
            float *da[2] = { la, ra }, *db[2] = { lb, rb };
            vban_jb_pull_f32(&jb_a, in_a, period);
            vban_jb_pull_f32(&jb_b, in_b, period);
            vban_f32_deinterleave(in_a, da, CH, period);
            vban_f32_deinterleave(in_b, db, CH, period);
            vban_mixer_process(&mx, ins, outs, period);
            vban_tx_write_f32_planar(&tx, (const float *const *)outs, period);
            next += (uint64_t)period * 1000000000ull / rate;
        }
        if (now >= stat) {
            fprintf(stderr, "\rA %.1f ms  B %.1f ms  peak L %.2f R %.2f  pings %u   ",
                    vban_jb_latency_us(&jb_a) / 1000.0, vban_jb_latency_us(&jb_b) / 1000.0,
                    vban_mixer_take_peak(&mx, 0), vban_mixer_take_peak(&mx, 1), rx.stats.pings_answered);
            stat = now + 1000000000ull;
        }
    }
    vban_udp_close(&s);
    vban_net_cleanup();
    return 0;
}
