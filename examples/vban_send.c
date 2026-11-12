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
 *   --ca ca.crt --cert me.crt --key me.key
 *              secure VBAN: handshake with the destination (a hub or vban_recv in
 *              secure mode), then every packet is encrypted and authenticated
 */
#include "common.h"
#include <math.h>

/* Secure mode: the transmitter's send callback seals each packet; handshake
 * replies are read between audio blocks. */
typedef struct {
    vban_socket_t  *s;
    vban_sec_id_t   id;
    vban_sec_link_t link;
    vban_addr_t     to;
    uint32_t        dropped;     /* audio before the session was up */
    int             was_up;
} sec_out_t;

static uint64_t now_ms(void) { return vban_time_ns() / 1000000u; }

static int sec_send(void *ctx, const uint8_t *pkt, size_t len, const vban_addr_t *to)
{
    sec_out_t *o = (sec_out_t *)ctx;
    uint8_t w[VBAN_PACKET_MAX_SIZE];
    int n = vban_sec_link_seal(&o->link, pkt, len, now_ms(), w, sizeof w);
    if (n < 0) { o->dropped++; return (int)len; }   /* not up yet: drop, keep going */
    return vban_udp_send(o->s, w, (size_t)n, to) < 0 ? VBAN_E_NET : (int)len;
}

static void sec_pump(sec_out_t *o)
{
    uint8_t in[VBAN_PACKET_MAX_SIZE], out[VBAN_PACKET_MAX_SIZE];
    vban_addr_t from;
    int n, ev, up;
    while ((n = vban_udp_recv(o->s, in, sizeof in, &from, 0)) > 0) {
        if (from.ip != o->to.ip || from.port != o->to.port) continue;
        n = vban_sec_link_handle(&o->link, &o->id, in, (size_t)n, now_ms(), out, sizeof out, &ev);
        if (n > 0 && ev == VBAN_SEC_EV_SEND) vban_udp_send(o->s, out, (size_t)n, &o->to);
        else if (n < 0 && n != VBAN_E_STATE) fprintf(stderr, "\nsecure: %s\n", vban_strerror(n));
    }
    if ((n = vban_sec_link_tick(&o->link, &o->id, now_ms(), out, sizeof out)) > 0)
        vban_udp_send(o->s, out, (size_t)n, &o->to);
    up = vban_sec_link_up(&o->link, now_ms());
    if (up && !o->was_up) {
        char nm[VBAN_CERT_NAME_MAX * 2 + 1];
        fprintf(stderr, "secure session up with '%s'\n",
                ex_safe_text(o->link.cur.peer.name, sizeof o->link.cur.peer.name, nm, sizeof nm));
    }
    o->was_up = up;
}

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
    static sec_out_t sec;
    int secure;

    ex_init();
    secure = ex_load_identity(argc, argv, &sec.id);
    if (secure < 0) return 2;
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
    if (secure) {
        if (VBAN_HEADER_SIZE + vban_pcm_bytes(tx.fpp, ch, c.datatype) > VBAN_SEC_INNER_MAX) {
            fprintf(stderr, "secure packets carry at most %d payload bytes: use fewer frames (-b)\n",
                    VBAN_SEC_INNER_MAX - VBAN_HEADER_SIZE);
            return 2;
        }
        sec.s = &s;
        sec.to = to;
        vban_sec_link_init(&sec.link);
        tx.send = sec_send;
        tx.send_ctx = &sec;
    }
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
        if (secure) sec_pump(&sec);
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
    if (secure) fprintf(stderr, "secure: %u packets dropped before the session was up\n", sec.dropped);
    vban_udp_close(&s);
    vban_net_cleanup();
    return 0;
}
