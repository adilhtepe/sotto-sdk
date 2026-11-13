/*
 * sotto - VBAN to and from any sound device, on Windows, macOS and Linux.
 *
 *   sotto devices                                   list playback and capture devices
 *   sotto play [options]                            receive a VBAN stream, play it on a device
 *   sotto send -i ip [options]                      capture a device, send it as a VBAN stream
 *
 * Options
 *   -n name        stream name (Stream1)            -p port   UDP port (6980)
 *   -d text        device whose name contains text (default: the system default)
 *   -c ch          channels (2)                     -r hz     sample rate for send (48000)
 *   -b frames      frames per packet for send (64 = 1.3 ms at 48 kHz)
 *   -l ms          play: initial buffering (10)     -s ip     play: only accept this source
 *   -f fmt         send: wire format s16 | s24 | f32 (s16)
 *   --ca f --cert f --key f      secure VBAN (vban_cert); play then accepts only authenticated senders
 *   --hub ip[:port]              play, secure: receive from a secure hub instead of listening
 *   --null         use a silent, real-time paced device (headless servers, tests)
 *
 * Audio runs in the device callback: play pulls from the SDK's lock-free jitter buffer (no lock,
 * no allocation); send pushes into a lock-free ring that the network thread drains every
 * millisecond, so sockets and encryption never run on the audio thread.
 *
 * On macOS and Windows the Sotto virtual sound cards appear as normal devices: pick them with -d.
 */
#define MINIAUDIO_IMPLEMENTATION
#define MA_NO_DECODING
#define MA_NO_ENCODING
#define MA_NO_GENERATION
#define MA_NO_RESOURCE_MANAGER
#define MA_NO_NODE_GRAPH
#define MA_NO_ENGINE
#include "miniaudio.h"

#include "../../examples/common.h"
#include "vban/vban_atomic.h"

static uint64_t now_ms(void) { return vban_time_ns() / 1000000u; }

/* ---------------------------------------------------------------- devices */

static int g_null;   /* --null: miniaudio's null backend, paced like a real device */

static ma_result open_context(ma_context *ctx)
{
    ma_backend null_backend = ma_backend_null;
    return ma_context_init(g_null ? &null_backend : NULL, g_null ? 1 : 0, NULL, ctx);
}

static int find_device(ma_context *ctx, ma_device_type type, const char *want, ma_device_id *out, char *name, size_t cap)
{
    ma_device_info *pb, *cp;
    ma_uint32 npb, ncp, i, n;
    ma_device_info *list;
    if (ma_context_get_devices(ctx, &pb, &npb, &cp, &ncp) != MA_SUCCESS)
        return -1;
    list = type == ma_device_type_playback ? pb : cp;
    n = type == ma_device_type_playback ? npb : ncp;
    for (i = 0; i < n; ++i)
        if (strstr(list[i].name, want)) {
            *out = list[i].id;
            snprintf(name, cap, "%s", list[i].name);
            return 0;
        }
    return -1;
}

static int cmd_devices(void)
{
    ma_context ctx;
    ma_device_info *pb, *cp;
    ma_uint32 npb, ncp, i;
    if (open_context(&ctx) != MA_SUCCESS || ma_context_get_devices(&ctx, &pb, &npb, &cp, &ncp) != MA_SUCCESS) {
        fprintf(stderr, "no audio backend available\n");
        return 1;
    }
    printf("backend: %s\nplayback:\n", ma_get_backend_name(ctx.backend));
    for (i = 0; i < npb; ++i) printf("  %s%s\n", pb[i].name, pb[i].isDefault ? "   (default)" : "");
    printf("capture:\n");
    for (i = 0; i < ncp; ++i) printf("  %s%s\n", cp[i].name, cp[i].isDefault ? "   (default)" : "");
    ma_context_uninit(&ctx);
    return 0;
}

/* ---------------------------------------------------------------- play */

static vban_jb_t g_jb;
static vban_atomic_u32 g_running;

static void play_cb(ma_device *dev, void *out, const void *in, ma_uint32 frames)
{
    (void)dev; (void)in;
    vban_jb_pull_f32(&g_jb, (float *)out, frames);   /* always fills: audio, concealment or silence */
}

typedef struct {
    vban_socket_t   *s;
    vban_rx_t       *rx;
    vban_sec_id_t    id;
    int              secure, use_hub;
    vban_sec_peer_t  peers[8];
    vban_sec_server_t srv;
    vban_sec_link_t  link;
    vban_addr_t      hub;
    int              was_up;
    uint32_t         dropped_plain, bad;
} play_net_t;

static void play_input(play_net_t *pn, const uint8_t *buf, size_t len, const vban_addr_t *from, uint64_t ns)
{
    uint8_t out[VBAN_PACKET_MAX_SIZE];
    int ev = 0, n;
    vban_sec_peer_t *peer = NULL;
    if (!pn->secure) {
        vban_rx_process(pn->rx, buf, len, from, ns);
        return;
    }
    if (len < VBAN_HEADER_SIZE || buf[4] != VBAN_PROTOCOL_USER || (pn->use_hub && (from->ip != pn->hub.ip || from->port != pn->hub.port))) {
        pn->dropped_plain++;
        return;
    }
    if (pn->use_hub)
        n = vban_sec_link_handle(&pn->link, &pn->id, buf, len, ns / 1000000u, out, sizeof out, &ev);
    else
        n = vban_sec_server_handle(&pn->srv, &pn->id, buf, len, from, ns / 1000000u, out, sizeof out, &ev, &peer);
    if (n < 0) { pn->bad++; return; }
    if (ev == VBAN_SEC_EV_SEND) vban_udp_send(pn->s, out, (size_t)n, from);
    else if (ev == VBAN_SEC_EV_DATA) vban_rx_process(pn->rx, out, (size_t)n, peer ? &peer->addr : from, ns);
}

static int cmd_play(int argc, char **argv)
{
    vban_socket_t s = VBAN_SOCKET_INIT;
    vban_rx_t rx;
    vban_jb_config_t jc;
    vban_addr_t src = { 0, 0 };
    static uint8_t bufs[VBAN_NET_BATCH][VBAN_PACKET_MAX_SIZE];
    static float ring[1 << 15];
    static play_net_t pn;
    vban_msg_t msgs[VBAN_NET_BATCH];
    ma_context ctx;
    ma_device dev;
    ma_device_config dc;
    ma_device_id did;
    char dname[256] = "default";
    const char *want = arg_str(argc, argv, "-d", NULL), *srcip = arg_str(argc, argv, "-s", NULL);
    const char *hub = arg_str(argc, argv, "--hub", NULL);
    unsigned ch = (unsigned)arg_int(argc, argv, "-c", 2), i;
    uint64_t next_stat;
    int quiet = arg_flag(argc, argv, "-q");

    pn.secure = ex_load_identity(argc, argv, &pn.id);
    if (pn.secure < 0) return 2;
    if (ch < 1 || ch > 8) { fprintf(stderr, "1..8 channels\n"); return 2; }
    if (srcip && vban_addr_parse(srcip, 0, &src) != VBAN_OK) { fprintf(stderr, "bad source ip\n"); return 2; }
    if (hub) {
        char host[64];
        const char *colon = strchr(hub, ':');
        size_t hl = colon ? (size_t)(colon - hub) : strlen(hub);
        if (!pn.secure || hl >= sizeof host) { fprintf(stderr, "--hub needs --ca/--cert/--key and ip[:port]\n"); return 2; }
        memcpy(host, hub, hl); host[hl] = 0;
        if (vban_addr_parse(host, colon ? (uint16_t)atoi(colon + 1) : VBAN_DEFAULT_PORT, &pn.hub) != VBAN_OK) { fprintf(stderr, "bad hub address\n"); return 2; }
        vban_sec_link_init(&pn.link);
        pn.use_hub = 1;
    } else if (pn.secure) {
        vban_sec_server_init(&pn.srv, pn.peers, 8, 20);
    }

    /* audio device first: its rate decides the jitter buffer's output rate */
    if (open_context(&ctx) != MA_SUCCESS) { fprintf(stderr, "no audio backend\n"); return 1; }
    dc = ma_device_config_init(ma_device_type_playback);
    if (want) {
        if (find_device(&ctx, ma_device_type_playback, want, &did, dname, sizeof dname)) { fprintf(stderr, "no playback device matching '%s' (see: sotto devices)\n", want); return 1; }
        dc.playback.pDeviceID = &did;
    }
    dc.playback.format = ma_format_f32;
    dc.playback.channels = ch;
    dc.sampleRate = 0;                         /* the device's own rate: the jitter buffer resamples */
    dc.periodSizeInFrames = 128;
    dc.performanceProfile = ma_performance_profile_low_latency;
    dc.dataCallback = play_cb;
    if (ma_device_init(&ctx, &dc, &dev) != MA_SUCCESS) { fprintf(stderr, "cannot open the playback device\n"); return 1; }

    memset(&jc, 0, sizeof jc);
    jc.channels = (uint16_t)ch;
    jc.out_rate = dev.sampleRate;
    jc.adaptive = 1;
    jc.drift = VBAN_JB_DRIFT_RESAMPLE;
    jc.target_frames = (uint32_t)(arg_int(argc, argv, "-l", 10) * 48);
    if (vban_jb_init(&g_jb, &jc, ring, (uint32_t)(sizeof ring / sizeof ring[0] / ch)) != VBAN_OK) { fprintf(stderr, "jitter buffer\n"); return 1; }

    vban_rx_init(&rx, NULL);
    vban_rx_subscribe_audio(&rx, arg_str(argc, argv, "-n", "Stream1"), src.ip, &g_jb, NULL, NULL);
    if (vban_udp_open(&s, NULL, hub ? 0 : (uint16_t)arg_int(argc, argv, "-p", VBAN_DEFAULT_PORT), VBAN_SOCK_REUSE) != VBAN_OK) {
        fprintf(stderr, "cannot bind the UDP port\n");
        return 1;
    }
    vban_udp_set_buffers(&s, 1 << 20, 0);
    pn.s = &s;
    pn.rx = &rx;
    for (i = 0; i < VBAN_NET_BATCH; ++i) { msgs[i].buf = bufs[i]; msgs[i].cap = sizeof bufs[i]; }

    if (ma_device_start(&dev) != MA_SUCCESS) { fprintf(stderr, "cannot start the device\n"); return 1; }
    fprintf(stderr, "playing '%s' on %s (%s, %u Hz, %u ch)%s\n", arg_str(argc, argv, "-n", "Stream1"), dname,
            ma_get_backend_name(ctx.backend), dev.sampleRate, ch, pn.secure ? ", secure" : "");
    vban_thread_realtime();
    next_stat = vban_time_ns() + 1000000000ull;
    while (!g_stop) {
        uint64_t ns;
        int n = vban_udp_recv_batch_us(&s, msgs, VBAN_NET_BATCH, 100000);
        ns = vban_time_ns();
        for (i = 0; n > 0 && i < (unsigned)n; ++i) play_input(&pn, msgs[i].buf, msgs[i].len, &msgs[i].addr, ns);
        if (pn.use_hub) {
            uint8_t hs[VBAN_SEC_HS_MAX];
            int up, k = vban_sec_link_tick(&pn.link, &pn.id, ns / 1000000u, hs, sizeof hs);
            if (k > 0) vban_udp_send(&s, hs, (size_t)k, &pn.hub);
            up = vban_sec_link_up(&pn.link, ns / 1000000u);
            if (up && !pn.was_up) fprintf(stderr, "\nsecure session up with the hub\n");
            pn.was_up = up;
        }
        if (!quiet && ns >= next_stat) {
            vban_jb_stats_t st;
            vban_jb_get_stats(&g_jb, &st);
            fprintf(stderr, "\r%u Hz  packets %u  lost %u  repaired %u  late %u  underruns %u  latency %.1f ms   ",
                    st.stream_rate, st.packets, st.lost, st.repaired, st.late, st.underruns, vban_jb_latency_us(&g_jb) / 1000.0);
            next_stat = ns + 1000000000ull;
        }
    }
    ma_device_uninit(&dev);
    ma_context_uninit(&ctx);
    vban_udp_close(&s);
    fprintf(stderr, "\n");
    return 0;
}

/* ---------------------------------------------------------------- send */

/* Single-producer (audio callback) / single-consumer (network thread) float ring. */
#define RING_FRAMES 8192u   /* power of two; 170 ms at 48 kHz */
static float g_ring[RING_FRAMES * 8];
static vban_atomic_u32 g_wr, g_rd, g_overflow;
static unsigned g_ch;

static void send_cb(ma_device *dev, void *out, const void *in, ma_uint32 frames)
{
    uint32_t wr = vban_load_rlx(&g_wr), rd = vban_load_acq(&g_rd), i;
    const float *src = (const float *)in;
    (void)dev; (void)out;
    if (RING_FRAMES - (wr - rd) < frames) {     /* network thread stalled: drop, never block */
        vban_store_rlx(&g_overflow, vban_load_rlx(&g_overflow) + 1);
        return;
    }
    for (i = 0; i < frames; ++i)
        memcpy(&g_ring[((wr + i) & (RING_FRAMES - 1)) * g_ch], src + (size_t)i * g_ch, sizeof(float) * g_ch);
    vban_store_rel(&g_wr, wr + frames);
}

typedef struct {
    vban_socket_t  *s;
    vban_sec_id_t   id;
    vban_sec_link_t link;
    vban_addr_t     to;
    int             secure, was_up;
    uint32_t        dropped;
} send_net_t;

static int sealing_send(void *ctx, const uint8_t *pkt, size_t len, const vban_addr_t *to)
{
    send_net_t *o = (send_net_t *)ctx;
    uint8_t w[VBAN_PACKET_MAX_SIZE];
    int n;
    if (!o->secure) return vban_udp_send(o->s, pkt, len, to);
    n = vban_sec_link_seal(&o->link, pkt, len, now_ms(), w, sizeof w);
    if (n < 0) { o->dropped++; return (int)len; }
    return vban_udp_send(o->s, w, (size_t)n, to) < 0 ? VBAN_E_NET : (int)len;
}

static int cmd_send(int argc, char **argv)
{
    vban_socket_t s = VBAN_SOCKET_INIT;
    vban_tx_t tx;
    vban_tx_config_t c;
    static send_net_t sn;
    ma_context ctx;
    ma_device dev;
    ma_device_config dc;
    ma_device_id did;
    char dname[256] = "default";
    const char *want = arg_str(argc, argv, "-d", NULL);
    unsigned ch = (unsigned)arg_int(argc, argv, "-c", 2);
    uint64_t next_stat;
    int quiet = arg_flag(argc, argv, "-q");

    sn.secure = ex_load_identity(argc, argv, &sn.id);
    if (sn.secure < 0) return 2;
    if (!arg_str(argc, argv, "-i", NULL)) { fprintf(stderr, "send needs -i destination\n"); return 2; }
    if (ch < 1 || ch > 8) { fprintf(stderr, "1..8 channels\n"); return 2; }
    g_ch = ch;
    memset(&c, 0, sizeof c);
    c.name = arg_str(argc, argv, "-n", "Stream1");
    c.sample_rate = (uint32_t)arg_int(argc, argv, "-r", 48000);
    c.channels = (uint16_t)ch;
    c.datatype = (uint8_t)parse_format(arg_str(argc, argv, "-f", "s16"));
    c.frames_per_packet = (uint16_t)arg_int(argc, argv, "-b", 64);
    c.send = sealing_send;
    c.send_ctx = &sn;
    if (vban_tx_init(&tx, &c) != VBAN_OK) { fprintf(stderr, "invalid stream format (rate must be a VBAN rate)\n"); return 2; }
    if (sn.secure && VBAN_HEADER_SIZE + vban_pcm_bytes(tx.fpp, ch, c.datatype) > VBAN_SEC_INNER_MAX) {
        fprintf(stderr, "secure packets carry at most %d payload bytes: use fewer frames (-b)\n", VBAN_SEC_INNER_MAX - VBAN_HEADER_SIZE);
        return 2;
    }
    if (vban_addr_parse(arg_str(argc, argv, "-i", NULL), (uint16_t)arg_int(argc, argv, "-p", VBAN_DEFAULT_PORT), &sn.to) ||
        vban_udp_open(&s, NULL, 0, VBAN_SOCK_BROADCAST | VBAN_SOCK_QOS) != VBAN_OK) {
        fprintf(stderr, "socket / address error\n");
        return 1;
    }
    sn.s = &s;
    vban_tx_add_dest(&tx, &sn.to);
    vban_sec_link_init(&sn.link);

    if (open_context(&ctx) != MA_SUCCESS) { fprintf(stderr, "no audio backend\n"); return 1; }
    dc = ma_device_config_init(ma_device_type_capture);
    if (want) {
        if (find_device(&ctx, ma_device_type_capture, want, &did, dname, sizeof dname)) { fprintf(stderr, "no capture device matching '%s' (see: sotto devices)\n", want); return 1; }
        dc.capture.pDeviceID = &did;
    }
    dc.capture.format = ma_format_f32;
    dc.capture.channels = ch;
    dc.sampleRate = c.sample_rate;             /* miniaudio converts if the device runs at another rate */
    dc.periodSizeInFrames = tx.fpp;
    dc.performanceProfile = ma_performance_profile_low_latency;
    dc.dataCallback = send_cb;
    if (ma_device_init(&ctx, &dc, &dev) != MA_SUCCESS || ma_device_start(&dev) != MA_SUCCESS) {
        fprintf(stderr, "cannot open the capture device\n");
        return 1;
    }
    fprintf(stderr, "sending '%s' from %s (%s) to %s, %u Hz %u ch, %u frames/packet%s\n", c.name, dname,
            ma_get_backend_name(ctx.backend), arg_str(argc, argv, "-i", ""), c.sample_rate, ch, tx.fpp, sn.secure ? ", secure" : "");
    vban_thread_realtime();
    next_stat = vban_time_ns() + 1000000000ull;
    while (!g_stop) {
        uint8_t in[VBAN_PACKET_MAX_SIZE], out[VBAN_PACKET_MAX_SIZE];
        vban_addr_t from;
        int n, ev;
        uint32_t wr, rd;
        /* wake every millisecond, or at once for a handshake reply */
        if (vban_udp_wait_us(&s, 1000) > 0)
            while ((n = vban_udp_recv(&s, in, sizeof in, &from, 0)) > 0)
                if (sn.secure && from.ip == sn.to.ip && from.port == sn.to.port) {
                    n = vban_sec_link_handle(&sn.link, &sn.id, in, (size_t)n, now_ms(), out, sizeof out, &ev);
                    if (n > 0 && ev == VBAN_SEC_EV_SEND) vban_udp_send(&s, out, (size_t)n, &sn.to);
                }
        if (sn.secure) {
            int up;
            if ((n = vban_sec_link_tick(&sn.link, &sn.id, now_ms(), out, sizeof out)) > 0) vban_udp_send(&s, out, (size_t)n, &sn.to);
            up = vban_sec_link_up(&sn.link, now_ms());
            if (up && !sn.was_up) fprintf(stderr, "secure session up\n");
            sn.was_up = up;
        }
        wr = vban_load_acq(&g_wr);
        rd = vban_load_rlx(&g_rd);
        while (rd != wr) {                     /* drain in contiguous chunks */
            uint32_t idx = rd & (RING_FRAMES - 1), chunk = wr - rd;
            if (chunk > RING_FRAMES - idx) chunk = RING_FRAMES - idx;
            vban_tx_write_f32(&tx, &g_ring[idx * ch], chunk);
            rd += chunk;
            vban_store_rel(&g_rd, rd);
        }
        if (!quiet && vban_time_ns() >= next_stat) {
            fprintf(stderr, "\rpackets %u  errors %u  overflows %u%s   ", tx.stats.packets, tx.stats.errors,
                    vban_load_rlx(&g_overflow), sn.secure && !sn.was_up ? "  (waiting for the secure session)" : "");
            next_stat = vban_time_ns() + 1000000000ull;
        }
    }
    ma_device_uninit(&dev);
    ma_context_uninit(&ctx);
    vban_udp_close(&s);
    fprintf(stderr, "\n");
    return 0;
}

int main(int argc, char **argv)
{
    const char *cmd = argc > 1 ? argv[1] : "";
    ex_init();
    vban_store_rel(&g_running, 1);
    g_null = arg_flag(argc, argv, "--null");
    if (!strcmp(cmd, "devices")) return cmd_devices();
    if (!strcmp(cmd, "play")) return cmd_play(argc, argv);
    if (!strcmp(cmd, "send")) return cmd_send(argc, argv);
    fprintf(stderr,
            "usage: sotto devices\n"
            "       sotto play [-n name] [-p port] [-d device] [-c ch] [-l ms] [-s ip] [--null] [--ca f --cert f --key f [--hub ip[:port]]]\n"
            "       sotto send -i ip [-n name] [-p port] [-d device] [-c ch] [-r hz] [-b frames] [-f s16|s24|f32] [--null] [--ca f --cert f --key f]\n");
    return 2;
}
