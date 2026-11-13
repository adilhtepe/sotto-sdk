/*
 * sotto-pw - Sotto virtual sound devices for Linux on PipeWire (no kernel module).
 *
 * Creates two devices every application sees, like a real sound card:
 *   "Sotto out"  a sink: whatever plays into it is sent as a VBAN stream   (needs -i)
 *   "Sotto in"   a source: a received VBAN stream, to record from          (needs -n / listening)
 *
 *   sotto-pw -i 192.168.1.20 -o Desktop -n Mic        both devices
 *   sotto-pw -n Mic                                    only "Sotto in"
 *   sotto-pw -i studio.example.net -o Desktop --no-in  only "Sotto out"
 *
 * Options
 *   -i host        destination of "Sotto out"       -o name   stream name sent (Linux)
 *   -n name        stream received into "Sotto in"  -p port   UDP port to listen on (6980)
 *   -c ch          channels (2)                     -b frames frames per packet sent (64)
 *   -l ms          initial buffering of "Sotto in" (10)          --no-in / --no-out
 *   --ca f --cert f --key f   secure VBAN; --hub host[:port] also receives through a secure hub
 *
 * PipeWire calls the process callbacks on its real-time thread: they only pull from the SDK's
 * lock-free jitter buffer or push into a lock-free ring. Sockets and encryption run on the main
 * thread, which wakes every millisecond.
 */
#include <pipewire/pipewire.h>
#include <spa/param/audio/format-utils.h>
#include "../../examples/common.h"
#include "vban/vban_atomic.h"

#define RATE 48000u
#define RING_FRAMES 8192u

static unsigned g_ch = 2;
static vban_jb_t g_jb;
static float g_ring[RING_FRAMES * 8];
static vban_atomic_u32 g_wr, g_rd, g_overflow;
static struct pw_stream *g_in_stream, *g_out_stream;

static uint64_t now_ms(void) { return vban_time_ns() / 1000000u; }

/* "Sotto in": the graph pulls, we fill from the jitter buffer (audio, concealment or silence) */
static void on_in_process(void *data)
{
    struct pw_buffer *b = pw_stream_dequeue_buffer(g_in_stream);
    struct spa_data *d;
    uint32_t stride = sizeof(float) * g_ch, frames;
    (void)data;
    if (!b) return;
    d = &b->buffer->datas[0];
    if (!d->data) { pw_stream_queue_buffer(g_in_stream, b); return; }
    frames = d->maxsize / stride;
    if (b->requested && b->requested < frames) frames = (uint32_t)b->requested;
    vban_jb_pull_f32(&g_jb, (float *)d->data, frames);
    d->chunk->offset = 0;
    d->chunk->stride = (int32_t)stride;
    d->chunk->size = frames * stride;
    pw_stream_queue_buffer(g_in_stream, b);
}

/* "Sotto out": applications played into us; hand it to the network thread through the ring */
static void on_out_process(void *data)
{
    struct pw_buffer *b = pw_stream_dequeue_buffer(g_out_stream);
    struct spa_data *d;
    uint32_t frames, wr, rd, i;
    const float *src;
    (void)data;
    if (!b) return;
    d = &b->buffer->datas[0];
    if (d->data && d->chunk->size) {
        src = (const float *)((const uint8_t *)d->data + d->chunk->offset);
        frames = d->chunk->size / (uint32_t)(sizeof(float) * g_ch);
        wr = vban_load_rlx(&g_wr);
        rd = vban_load_acq(&g_rd);
        if (RING_FRAMES - (wr - rd) < frames) {
            vban_store_rlx(&g_overflow, vban_load_rlx(&g_overflow) + 1);
        } else {
            for (i = 0; i < frames; ++i)
                memcpy(&g_ring[((wr + i) & (RING_FRAMES - 1)) * g_ch], src + (size_t)i * g_ch, sizeof(float) * g_ch);
            vban_store_rel(&g_wr, wr + frames);
        }
    }
    pw_stream_queue_buffer(g_out_stream, b);
}

static const struct pw_stream_events in_events = { PW_VERSION_STREAM_EVENTS, .process = on_in_process };
static const struct pw_stream_events out_events = { PW_VERSION_STREAM_EVENTS, .process = on_out_process };

static struct pw_stream *make_node(struct pw_thread_loop *loop, int sink, const char *node, const char *desc)
{
    uint8_t buffer[1024];
    struct spa_pod_builder b = SPA_POD_BUILDER_INIT(buffer, sizeof(buffer));
    const struct spa_pod *params[1];
    struct spa_audio_info_raw info;
    struct pw_stream *s;
    struct pw_properties *props = pw_properties_new(
        PW_KEY_MEDIA_TYPE, "Audio",
        PW_KEY_MEDIA_CATEGORY, sink ? "Capture" : "Playback",
        PW_KEY_MEDIA_CLASS, sink ? "Audio/Sink" : "Audio/Source",
        PW_KEY_NODE_NAME, node,
        PW_KEY_NODE_DESCRIPTION, desc,
        PW_KEY_NODE_LATENCY, "128/48000",
        PW_KEY_NODE_VIRTUAL, "true",
        NULL);
    memset(&info, 0, sizeof info);
    info.format = SPA_AUDIO_FORMAT_F32;
    info.channels = g_ch;
    info.rate = RATE;
    if (g_ch == 2) { info.position[0] = SPA_AUDIO_CHANNEL_FL; info.position[1] = SPA_AUDIO_CHANNEL_FR; }
    else if (g_ch == 1) info.position[0] = SPA_AUDIO_CHANNEL_MONO;
    else info.flags = SPA_AUDIO_FLAG_UNPOSITIONED;
    params[0] = spa_format_audio_raw_build(&b, SPA_PARAM_EnumFormat, &info);
    s = pw_stream_new_simple(pw_thread_loop_get_loop(loop), node, props, sink ? &out_events : &in_events, NULL);
    if (!s || pw_stream_connect(s, sink ? PW_DIRECTION_INPUT : PW_DIRECTION_OUTPUT, PW_ID_ANY,
                                PW_STREAM_FLAG_MAP_BUFFERS | PW_STREAM_FLAG_RT_PROCESS, params, 1) < 0)
        return NULL;
    return s;
}

/* network side: the same secure pieces as sotto / the SDK examples */
typedef struct {
    vban_socket_t     *s;
    vban_sec_id_t      id;
    int                secure, use_hub, was_up;
    vban_sec_link_t    out_link;     /* "Sotto out" to its destination */
    vban_sec_link_t    hub_link;     /* "Sotto in" through a secure hub */
    vban_sec_peer_t    peers[8];
    vban_sec_server_t  srv;
    vban_addr_t        to, hub;
    vban_rx_t          rx;
    uint32_t           dropped;
} net_t;

static int sealing_send(void *ctx, const uint8_t *pkt, size_t len, const vban_addr_t *to)
{
    net_t *n = (net_t *)ctx;
    uint8_t w[VBAN_PACKET_MAX_SIZE];
    int k;
    if (!n->secure) return vban_udp_send(n->s, pkt, len, to);
    k = vban_sec_link_seal(&n->out_link, pkt, len, now_ms(), w, sizeof w);
    if (k < 0) { n->dropped++; return (int)len; }
    return vban_udp_send(n->s, w, (size_t)k, to) < 0 ? VBAN_E_NET : (int)len;
}

static void net_input(net_t *n, const uint8_t *buf, size_t len, const vban_addr_t *from, uint64_t ns)
{
    uint8_t out[VBAN_PACKET_MAX_SIZE];
    vban_sec_peer_t *peer = NULL;
    int ev = 0, k;
    if (!n->secure) { vban_rx_process(&n->rx, buf, len, from, ns); return; }
    if (len < VBAN_HEADER_SIZE || buf[4] != VBAN_PROTOCOL_USER) return;   /* secure: plain VBAN is ignored */
    if (from->ip == n->to.ip && from->port == n->to.port)
        k = vban_sec_link_handle(&n->out_link, &n->id, buf, len, ns / 1000000u, out, sizeof out, &ev);
    else if (n->use_hub && from->ip == n->hub.ip && from->port == n->hub.port)
        k = vban_sec_link_handle(&n->hub_link, &n->id, buf, len, ns / 1000000u, out, sizeof out, &ev);
    else if (!n->use_hub)
        k = vban_sec_server_handle(&n->srv, &n->id, buf, len, from, ns / 1000000u, out, sizeof out, &ev, &peer);
    else
        return;
    if (k < 0) return;
    if (ev == VBAN_SEC_EV_SEND) vban_udp_send(n->s, out, (size_t)k, from);
    else if (ev == VBAN_SEC_EV_DATA) vban_rx_process(&n->rx, out, (size_t)k, peer ? &peer->addr : from, ns);
}

static int parse_hostport(const char *hp, vban_addr_t *out)
{
    char host[128];
    const char *colon = strchr(hp, ':');
    size_t hl = colon ? (size_t)(colon - hp) : strlen(hp);
    if (hl >= sizeof host) return -1;
    memcpy(host, hp, hl);
    host[hl] = 0;
    return vban_addr_parse(host, colon ? (uint16_t)atoi(colon + 1) : VBAN_DEFAULT_PORT, out);
}

int main(int argc, char **argv)
{
    static net_t n;
    static float jbring[1 << 15];
    static uint8_t bufs[VBAN_NET_BATCH][VBAN_PACKET_MAX_SIZE];
    vban_msg_t msgs[VBAN_NET_BATCH];
    vban_socket_t s = VBAN_SOCKET_INIT;
    vban_tx_t tx;
    vban_tx_config_t tc;
    vban_jb_config_t jc;
    struct pw_thread_loop *loop;
    const char *dest = arg_str(argc, argv, "-i", NULL), *hub = arg_str(argc, argv, "--hub", NULL);
    int want_in = !arg_flag(argc, argv, "--no-in"), want_out = dest && !arg_flag(argc, argv, "--no-out");
    unsigned i;

    ex_init();
    g_ch = (unsigned)arg_int(argc, argv, "-c", 2);
    if (g_ch < 1 || g_ch > 8) { fprintf(stderr, "1..8 channels\n"); return 2; }
    if (!want_in && !want_out) {
        fprintf(stderr, "usage: sotto-pw [-i host] [-o name] [-n name] [-p port] [-c ch] [-b frames] [-l ms] [--no-in] [--no-out]\n"
                        "                [--ca f --cert f --key f [--hub host[:port]]]\n");
        return 2;
    }
    n.secure = ex_load_identity(argc, argv, &n.id);
    if (n.secure < 0) return 2;
    if (dest && parse_hostport(dest, &n.to)) { fprintf(stderr, "bad destination\n"); return 2; }
    if (hub) {
        if (!n.secure || parse_hostport(hub, &n.hub)) { fprintf(stderr, "--hub needs --ca/--cert/--key and host[:port]\n"); return 2; }
        n.use_hub = 1;
    }
    vban_sec_link_init(&n.out_link);
    vban_sec_link_init(&n.hub_link);
    if (n.secure && !hub) vban_sec_server_init(&n.srv, n.peers, 8, 20);

    /* receive side: jitter buffer at the graph rate */
    memset(&jc, 0, sizeof jc);
    jc.channels = (uint16_t)g_ch;
    jc.out_rate = RATE;
    jc.adaptive = 1;
    jc.drift = VBAN_JB_DRIFT_RESAMPLE;
    jc.target_frames = (uint32_t)(arg_int(argc, argv, "-l", 10) * 48);
    vban_jb_init(&g_jb, &jc, jbring, (uint32_t)(sizeof jbring / sizeof jbring[0] / g_ch));
    vban_rx_init(&n.rx, NULL);
    vban_rx_subscribe_audio(&n.rx, arg_str(argc, argv, "-n", "Stream1"), 0, &g_jb, NULL, NULL);

    /* send side */
    memset(&tc, 0, sizeof tc);
    tc.name = arg_str(argc, argv, "-o", "Linux");
    tc.sample_rate = RATE;
    tc.channels = (uint16_t)g_ch;
    tc.datatype = VBAN_DATATYPE_S16;
    tc.frames_per_packet = (uint16_t)arg_int(argc, argv, "-b", 64);
    tc.send = sealing_send;
    tc.send_ctx = &n;
    if (vban_tx_init(&tx, &tc) != VBAN_OK) { fprintf(stderr, "bad send format\n"); return 2; }
    if (want_out) vban_tx_add_dest(&tx, &n.to);

    if (vban_udp_open(&s, NULL, (want_in && !hub) ? (uint16_t)arg_int(argc, argv, "-p", VBAN_DEFAULT_PORT) : 0,
                      VBAN_SOCK_REUSE | VBAN_SOCK_BROADCAST | VBAN_SOCK_QOS) != VBAN_OK) {
        fprintf(stderr, "cannot open the UDP socket (port in use?)\n");
        return 1;
    }
    vban_udp_set_buffers(&s, 1 << 20, 0);
    n.s = &s;

    pw_init(&argc, &argv);
    loop = pw_thread_loop_new("sotto", NULL);
    if (!loop || pw_thread_loop_start(loop) < 0) { fprintf(stderr, "cannot start PipeWire\n"); return 1; }
    pw_thread_loop_lock(loop);
    if (want_in) g_in_stream = make_node(loop, 0, "sotto_in", "Sotto in");
    if (want_out) g_out_stream = make_node(loop, 1, "sotto_out", "Sotto out");
    pw_thread_loop_unlock(loop);
    if ((want_in && !g_in_stream) || (want_out && !g_out_stream)) { fprintf(stderr, "cannot create the PipeWire nodes (is PipeWire running?)\n"); return 1; }
    fprintf(stderr, "%s%s%s%s\n", want_in ? "\"Sotto in\" <- VBAN " : "", want_in ? arg_str(argc, argv, "-n", "Stream1") : "",
            want_out ? (want_in ? ", \"Sotto out\" -> " : "\"Sotto out\" -> ") : "", want_out ? dest : "");

    for (i = 0; i < VBAN_NET_BATCH; ++i) { msgs[i].buf = bufs[i]; msgs[i].cap = sizeof bufs[i]; }
    vban_thread_realtime();
    while (!g_stop) {
        uint8_t hs[VBAN_SEC_HS_MAX];
        uint64_t ns;
        uint32_t wr, rd;
        int k = vban_udp_recv_batch_us(&s, msgs, VBAN_NET_BATCH, 1000);
        ns = vban_time_ns();
        for (i = 0; k > 0 && i < (unsigned)k; ++i) net_input(&n, msgs[i].buf, msgs[i].len, &msgs[i].addr, ns);
        if (n.secure && want_out && (k = vban_sec_link_tick(&n.out_link, &n.id, ns / 1000000u, hs, sizeof hs)) > 0)
            vban_udp_send(&s, hs, (size_t)k, &n.to);
        if (n.use_hub && (k = vban_sec_link_tick(&n.hub_link, &n.id, ns / 1000000u, hs, sizeof hs)) > 0)
            vban_udp_send(&s, hs, (size_t)k, &n.hub);
        wr = vban_load_acq(&g_wr);
        rd = vban_load_rlx(&g_rd);
        while (rd != wr) {
            uint32_t idx = rd & (RING_FRAMES - 1), chunk = wr - rd;
            if (chunk > RING_FRAMES - idx) chunk = RING_FRAMES - idx;
            vban_tx_write_f32(&tx, &g_ring[idx * g_ch], chunk);
            rd += chunk;
            vban_store_rel(&g_rd, rd);
        }
    }
    pw_thread_loop_lock(loop);
    if (g_in_stream) pw_stream_destroy(g_in_stream);
    if (g_out_stream) pw_stream_destroy(g_out_stream);
    pw_thread_loop_unlock(loop);
    pw_thread_loop_stop(loop);
    pw_thread_loop_destroy(loop);
    pw_deinit();
    vban_udp_close(&s);
    return 0;
}
