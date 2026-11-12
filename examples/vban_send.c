/*
 * vban_send - transmit a test tone or raw PCM from stdin as a VBAN stream.
 *
 *   vban_send -i 192.168.1.10 -n Stream1 -r 48000 -c 2 -f s16 -b 64
 *   arecord -f S16_LE -r 48000 -c 2 | vban_send -i 192.168.1.10 --stdin
 *
 *   -i ip      destination (default 127.0.0.1; broadcast allowed)
 *   -p port    (6980)        -n name   stream name (Stream1)
 *   -r rate    (48000)       -c ch     channels (2)
 *   -f fmt     wire format u8|s16|s24|s32|f32|f64 (s16)
 *   -b frames  frames per packet, 1..256 (64 = 1.3 ms @48k)
 *   --stdin    read interleaved s16le from stdin instead of the tone
 *   -t hz      tone frequency (440)
 *   -R n       resend every packet n packets later (loss protection, 2x bandwidth;
 *              ~5 ms is a good delay: 4 at 64 frames / 48 kHz)
 */
#include "common.h"
#include <math.h>

int main(int argc, char **argv)
{
    vban_socket_t s = VBAN_SOCKET_INIT;
    vban_tx_t tx;
    vban_tx_config_t c;
    vban_addr_t to;
    vban_pacer_t pacer;
    int use_stdin = arg_flag(argc, argv, "--stdin");
    unsigned ch = (unsigned)arg_int(argc, argv, "-c", 2), i, k;
    double phase = 0, freq = (double)arg_int(argc, argv, "-t", 440);
    enum { BLOCK = 64 };
    static float buf[BLOCK * 256];
    static int16_t ibuf[BLOCK * 256];

    ex_init();
    memset(&c, 0, sizeof c);
    c.name = arg_str(argc, argv, "-n", "Stream1");
    c.sample_rate = (uint32_t)arg_int(argc, argv, "-r", 48000);
    c.channels = (uint16_t)ch;
    c.datatype = (uint8_t)parse_format(arg_str(argc, argv, "-f", "s16"));
    c.frames_per_packet = (uint16_t)arg_int(argc, argv, "-b", 64);
    c.send = vban_udp_sink;
    c.send_ctx = &s;
    if (ch < 1 || ch > 256 || vban_tx_init(&tx, &c) != VBAN_OK) {
        fprintf(stderr, "invalid stream format (rate must be a VBAN rate)\n");
        return 2;
    }
    if (vban_addr_parse(arg_str(argc, argv, "-i", "127.0.0.1"), (uint16_t)arg_int(argc, argv, "-p", VBAN_DEFAULT_PORT), &to) ||
        vban_udp_open(&s, NULL, 0, VBAN_SOCK_BROADCAST | VBAN_SOCK_QOS) != VBAN_OK) {
        fprintf(stderr, "socket / address error\n");
        return 1;
    }
    vban_tx_add_dest(&tx, &to);
    {
        static uint8_t red_hist[VBAN_TX_REDUNDANCY_MAX * VBAN_PACKET_MAX_SIZE];
        long red = arg_int(argc, argv, "-R", 0);
        if (red > 0 && vban_tx_set_redundancy(&tx, (unsigned)red, red_hist, sizeof red_hist) != VBAN_OK) {
            fprintf(stderr, "redundancy delay must be 1..%d\n", VBAN_TX_REDUNDANCY_MAX);
            return 2;
        }
    }
    vban_thread_realtime();
    fprintf(stderr, "sending '%s' %u Hz %u ch %s, %u frames/packet (%.2f ms)\n", c.name, c.sample_rate, ch,
            format_name(c.datatype), tx.fpp, tx.fpp * 1000.0 / c.sample_rate);

    vban_pacer_init(&pacer, c.sample_rate);
    while (!g_stop) {
        if (use_stdin) {
            size_t got = fread(ibuf, sizeof(int16_t) * ch, BLOCK, stdin);
            if (got == 0) break;
            vban_tx_write_s16(&tx, ibuf, got);
            /* stdin sources are usually live (arecord): they pace themselves */
        } else {
            for (i = 0; i < BLOCK; ++i) {
                float v = (float)(0.25 * sin(phase));
                phase += 2 * 3.14159265358979 * freq / c.sample_rate;
                if (phase > 6.283185307179586) phase -= 6.283185307179586;
                for (k = 0; k < ch; ++k) buf[i * ch + k] = v;
            }
            vban_tx_write_f32(&tx, buf, BLOCK);
            vban_pacer_wait(&pacer, BLOCK);
        }
    }
    vban_tx_flush(&tx);
    fprintf(stderr, "\n%u packets, %u errors\n", tx.stats.packets, tx.stats.errors);
    vban_udp_close(&s);
    vban_net_cleanup();
    return 0;
}
