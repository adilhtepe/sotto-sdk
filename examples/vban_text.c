/*
 * vban_text - send a TEXT command (Voicemeeter remote scripting) and print
 * any REQUESTREPLY / text answer that comes back.
 *
 *   vban_text -i 192.168.1.10 "Strip[0].Mute=1;Bus[0].Gain=-6;"
 *   vban_text -i 192.168.1.10 -n Command1 -w 500 "Command.Version = ?;"
 */
#include "common.h"

static void on_reply(void *user, const vban_packet_t *p, const vban_addr_t *from)
{
    (void)user; (void)from;
    if (p->protocol == VBAN_PROTOCOL_TXT ||
        (p->protocol == VBAN_PROTOCOL_SERVICE && vban_service_type(p) == VBAN_SERVICE_REQUESTREPLY))
    {
        char safe[VBAN_DATA_MAX_SIZE + 1];
        printf("%s\n", ex_safe_text((const char *)p->data, p->data_len, safe, sizeof safe));
    }
}

int main(int argc, char **argv)
{
    vban_socket_t s = VBAN_SOCKET_INIT;
    vban_out_t out;
    vban_addr_t to;
    vban_rx_t rx;
    vban_rx_config_t rc;
    uint8_t buf[VBAN_PACKET_MAX_SIZE];
    const char *text = argc > 1 ? argv[argc - 1] : NULL;
    uint64_t end;
    int r;

    ex_init();
    if (!text || text[0] == '-') {
        fprintf(stderr, "usage: vban_text -i ip [-p port] [-n Command1] [-w wait_ms] \"text\"\n");
        return 2;
    }
    if (vban_addr_parse(arg_str(argc, argv, "-i", "127.0.0.1"), (uint16_t)arg_int(argc, argv, "-p", 6980), &to) ||
        vban_udp_open(&s, NULL, 0, VBAN_SOCK_BROADCAST) != VBAN_OK) {
        fprintf(stderr, "socket / address error\n");
        return 1;
    }
    vban_out_init(&out, arg_str(argc, argv, "-n", "Command1"), &to, vban_udp_sink, &s);
    r = vban_send_text(&out, text, -1);
    if (r < 0) { fprintf(stderr, "send failed: %s\n", vban_strerror(r)); return 1; }

    memset(&rc, 0, sizeof rc);
    rc.on_service = on_reply;
    rc.on_unmatched = on_reply;
    vban_rx_init(&rx, &rc);
    end = vban_time_ns() + (uint64_t)arg_int(argc, argv, "-w", 300) * 1000000ull;
    while (!g_stop) {
        vban_addr_t from;
        uint64_t now = vban_time_ns();
        int n;
        if (now >= end) break;
        n = vban_udp_recv(&s, buf, sizeof buf, &from, (int)((end - now) / 1000000) + 1);
        if (n > 0) vban_rx_process(&rx, buf, (size_t)n, &from, 0);
    }
    vban_udp_close(&s);
    vban_net_cleanup();
    return 0;
}
