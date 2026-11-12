/*
 * vban_monitor - list every VBAN stream arriving on a port (all sub
 * protocols), with format, packet rate and loss. Like a "streams heard" view.
 *
 *   vban_monitor [-p 6980]
 */
#include "common.h"

#define MAXS 32
static vban_heard_t g_tab[MAXS];
static uint32_t g_prev[MAXS];
static vban_seq_t g_seq[MAXS];
static uint32_t g_lost[MAXS];

static void on_any(void *user, const vban_packet_t *p, const vban_addr_t *from)
{
    uint64_t now = *(const uint64_t *)user;
    int i = vban_heard_update(g_tab, MAXS, p, from, now);
    if (i < 0) return;
    uint32_t lost = 0;
    if (g_tab[i].packets == 1) { vban_seq_init(&g_seq[i], 1000); g_lost[i] = 0; g_prev[i] = 0; }
    if (vban_seq_update(&g_seq[i], p->nu_frame, &lost) == VBAN_SEQ_GAP) g_lost[i] += lost;
}

int main(int argc, char **argv)
{
    vban_socket_t s = VBAN_SOCKET_INIT;
    vban_rx_t rx;
    vban_rx_config_t rc;
    static uint8_t bufs[VBAN_NET_BATCH][VBAN_PACKET_MAX_SIZE];
    vban_msg_t msgs[VBAN_NET_BATCH];
    uint64_t now = 0, next;
    unsigned i;

    ex_init();
    if (vban_udp_open(&s, NULL, (uint16_t)arg_int(argc, argv, "-p", 6980), VBAN_SOCK_REUSE) != VBAN_OK) {
        fprintf(stderr, "cannot bind\n");
        return 1;
    }
    memset(&rc, 0, sizeof rc);
    rc.on_unmatched = on_any;
    rc.on_service = on_any;
    rc.user = &now;
    vban_rx_init(&rx, &rc);
    for (i = 0; i < VBAN_NET_BATCH; ++i) { msgs[i].buf = bufs[i]; msgs[i].cap = sizeof bufs[i]; }
    next = vban_time_ns() + 1000000000ull;
    while (!g_stop) {
        int n = vban_udp_recv_batch(&s, msgs, VBAN_NET_BATCH, 200);
        now = vban_time_ns();
        for (i = 0; n > 0 && i < (unsigned)n; ++i) vban_rx_process(&rx, msgs[i].buf, msgs[i].len, &msgs[i].addr, now);
        if (now >= next) {
            printf("\033[2J\033[H%-21s %-16s %-7s %-8s %-4s %-4s %8s %8s\n", "source", "name", "proto", "rate",
                   "ch", "fmt", "pkt/s", "lost");
            for (i = 0; i < MAXS; ++i) {
                vban_heard_t *h = &g_tab[i];
                char a[22], nm[17];
                if (!h->packets || now - h->last_ns > 5000000000ull) continue;
                vban_addr_format(&h->from, a);
                {
                    char raw[17];
                    vban_name_get(&h->name, raw);
                    ex_safe_text(raw, sizeof raw, nm, sizeof nm);
                }
                if (h->protocol == VBAN_PROTOCOL_AUDIO)
                    printf("%-21s %-16s %-7s %-8u %-4u %-4s %8u %8u\n", a, nm, protocol_name(h->protocol),
                           vban_sr_from_index(h->format_SR & 31), h->format_nbc + 1u, format_name(h->format_bit),
                           h->packets - g_prev[i], g_lost[i]);
                else
                    printf("%-21s %-16s %-7s %-8s %-4s %-4s %8u %8u\n", a, nm, protocol_name(h->protocol), "-", "-",
                           "-", h->packets - g_prev[i], g_lost[i]);
                g_prev[i] = h->packets;
            }
            fflush(stdout);
            next = now + 1000000000ull;
        }
    }
    vban_udp_close(&s);
    vban_net_cleanup();
    return 0;
}
