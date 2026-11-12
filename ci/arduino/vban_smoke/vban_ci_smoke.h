/*
 * Shared compile/link smoke body for the ESP-IDF and Arduino CI builds.
 * It touches every module (proto, pcm, jitter, mixer, rx, tx, net/clock)
 * so the linker has to resolve the whole SDK for the target toolchain.
 * It is built, not flashed: CI has no hardware. Buffers are kept small
 * so the sketch also fits an 8 KB-RAM AVR (Arduino Mega).
 */
#ifndef VBAN_CI_SMOKE_H
#define VBAN_CI_SMOKE_H

#include "vban.h"
#include <string.h>

static unsigned smoke_sent;

static int smoke_send(void *ctx, const uint8_t *pkt, size_t len, const vban_addr_t *to)
{
    (void)ctx; (void)pkt; (void)to;
    ++smoke_sent;
    return (int)len;
}

static int vban_ci_smoke(void)
{
    static vban_tx_t tx;
    static vban_rx_t rx;
    static vban_jb_t jb;
    static int16_t ring[2 * 256]; /* 256 frames is the minimum capacity */
    static float in[2 * 32], out[2 * 32];
    static uint64_t mixmem[64];
    static vban_mixer_t mx;
    vban_tx_config_t tc;
    vban_rx_config_t rc;
    vban_jb_config_t jc;
    vban_addr_t to;
    uint64_t t0;

    memset(&tc, 0, sizeof tc);
    tc.name = "ci"; tc.sample_rate = 48000; tc.channels = 2;
    tc.datatype = VBAN_DATATYPE_S16; tc.frames_per_packet = 32;
    tc.send = smoke_send;
    if (vban_tx_init(&tx, &tc) != VBAN_OK) return 1;
    if (vban_addr_parse("127.0.0.1", VBAN_DEFAULT_PORT, &to) != VBAN_OK) return 2;
    vban_tx_add_dest(&tx, &to);

    memset(&jc, 0, sizeof jc);
    jc.channels = 2; jc.target_frames = 64; jc.storage = VBAN_JB_S16;
    if (vban_jb_storage_bytes(256, 2, VBAN_JB_S16) > sizeof ring) return 3;
    if (vban_jb_init(&jb, &jc, ring, 256) != VBAN_OK) return 4;

    memset(&rc, 0, sizeof rc);
    vban_rx_init(&rx, &rc);
    vban_rx_subscribe_audio(&rx, "ci", 0, &jb, NULL, NULL);

    if (vban_mixer_mem_size(2, 2) > sizeof mixmem) return 5;
    vban_mixer_init(&mx, mixmem, sizeof mixmem, 2, 2);

    memset(in, 0, sizeof in);
    vban_tx_write_f32(&tx, in, 32);
    vban_rx_process(&rx, tx.pkt, VBAN_HEADER_SIZE + 32 * 2 * 2, &to, vban_time_ns());
    vban_jb_pull_f32(&jb, out, 32);
    vban_mixer_process_interleaved(&mx, out, in, 32);

    t0 = vban_time_ns();
    vban_sleep_until_ns(t0 + 1000);
    vban_thread_realtime();
#if VBAN_ENABLE_NET
    {
        vban_socket_t s = VBAN_SOCKET_INIT;
        if (vban_net_init() == VBAN_OK &&
            vban_udp_open(&s, NULL, 0, VBAN_SOCK_BROADCAST | VBAN_SOCK_NONBLOCK) == VBAN_OK) {
            static uint8_t b[VBAN_PACKET_MAX_SIZE];
            vban_msg_t m;
            m.buf = b; m.cap = sizeof b; m.len = 0;
            vban_udp_send(&s, tx.pkt, VBAN_HEADER_SIZE, &to);
            vban_udp_recv_batch(&s, &m, 1, 0);
            vban_udp_close(&s);
        }
    }
#endif
    return smoke_sent ? 0 : 6;
}

#endif /* VBAN_CI_SMOKE_H */
