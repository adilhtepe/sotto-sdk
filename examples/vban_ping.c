/*
 * vban_ping - discover VBAN devices with the PING0 identification service.
 *
 *   vban_ping                     broadcast on 255.255.255.255:6980
 *   vban_ping -i 192.168.1.255    subnet broadcast or a single host
 *   vban_ping -w 2000             wait time in ms (1000)
 */
#include "common.h"

static uint32_t g_id;
static int g_found;

static void on_service(void *user, const vban_packet_t *p, const vban_addr_t *from)
{
    vban_ping0_t info;
    char a[22], dev[64], app[64], host[64];
    (void)user;
    if (vban_service_type(p) != VBAN_SERVICE_IDENTIFICATION || !vban_service_is_reply(p)) return;
    if (p->nu_frame != g_id) return; /* not an answer to our request */
    vban_ping0_decode(p->data, p->data_len, &info);
    vban_addr_format(from, a);
    ex_safe_text(info.device_name, sizeof info.device_name, dev, sizeof dev);
    ex_safe_text(info.application_name, sizeof info.application_name, app, sizeof app);
    ex_safe_text(info.host_name, sizeof info.host_name, host, sizeof host);
    printf("%-21s  %-24s  %-20s  %-16s  type 0x%08X  feat 0x%08X  %u Hz (%u-%u)  v%u.%u.%u.%u\n",
           a, dev[0] ? dev : "?", app, host,
           info.bit_type, info.bit_feature, info.prefered_rate, info.min_rate, info.max_rate,
           info.version[3], info.version[2], info.version[1], info.version[0]);
    ++g_found;
}

int main(int argc, char **argv)
{
    vban_socket_t s = VBAN_SOCKET_INIT;
    vban_rx_t rx;
    vban_rx_config_t rc;
    vban_out_t out;
    vban_addr_t to;
    vban_ping0_t me;
    uint8_t buf[VBAN_PACKET_MAX_SIZE];
    uint64_t end;

    ex_init();
    if (vban_udp_open(&s, NULL, 0, VBAN_SOCK_BROADCAST) != VBAN_OK ||
        vban_addr_parse(arg_str(argc, argv, "-i", "255.255.255.255"), (uint16_t)arg_int(argc, argv, "-p", 6980), &to)) {
        fprintf(stderr, "socket / address error\n");
        return 1;
    }
    memset(&me, 0, sizeof me);
    me.bit_type = VBAN_PING_TYPE_RECEPTOR;
    me.bit_feature = VBAN_PING_FEATURE_AUDIO;
    strcpy(me.application_name, "vban_ping (VBAN SDK)");
    strcpy(me.device_name, "vban_ping");

    memset(&rc, 0, sizeof rc);
    rc.on_service = on_service;
    vban_rx_init(&rx, &rc);

    vban_out_init(&out, "VBAN SPOT PING", &to, vban_udp_sink, &s);
    out.frame = (uint32_t)(vban_time_ns() & 0x7FFFFFFF);
    g_id = out.frame;
    if (vban_send_ping(&out, &me) < 0) { fprintf(stderr, "send failed\n"); return 1; }

    end = vban_time_ns() + (uint64_t)arg_int(argc, argv, "-w", 1000) * 1000000ull;
    while (!g_stop) {
        vban_addr_t from;
        uint64_t now = vban_time_ns();
        int n;
        if (now >= end) break;
        n = vban_udp_recv(&s, buf, sizeof buf, &from, (int)((end - now) / 1000000) + 1);
        if (n > 0) vban_rx_process(&rx, buf, (size_t)n, &from, 0);
    }
    fprintf(stderr, "%d device(s)\n", g_found);
    vban_udp_close(&s);
    vban_net_cleanup();
    return 0;
}
