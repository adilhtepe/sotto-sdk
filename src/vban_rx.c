/* VBAN SDK - receiver. See vban/vban_rx.h. */
#include "vban/vban_rx.h"
#include "vban/vban_net.h" /* vban_time_ns() when the caller passes now == 0 */
#include <string.h>

static const vban_name_t k_any_name = { { 0 } }; /* all zero = any */

void vban_rx_init(vban_rx_t *rx, const vban_rx_config_t *cfg)
{
    memset(rx, 0, sizeof *rx);
    if (cfg) rx->cfg = *cfg;
    rx->ping_tokens = VBAN_PING_REPLY_RATE;
    rx->ping_tokens_public = VBAN_PING_REPLY_RATE_PUBLIC;
}

int vban_rx_subscribe(vban_rx_t *rx, const char *name, uint32_t ip, uint8_t protocol,
                      vban_rx_cb cb, void *user)
{
    int i;
    for (i = 0; i < VBAN_RX_MAX_STREAMS; ++i) {
        vban_rx_stream_t *s = &rx->streams[i];
        if (s->active) continue;
        memset(s, 0, sizeof *s);
        vban_name_set(&s->name, name);
        s->ip = ip;
        s->protocol = protocol;
        s->cb = cb;
        s->user = user;
        s->active = 1;
        return i;
    }
    return VBAN_E_NOSPACE;
}

int vban_rx_subscribe_audio(vban_rx_t *rx, const char *name, uint32_t ip, vban_jb_t *jb,
                            vban_rx_cb cb, void *user)
{
    int id = vban_rx_subscribe(rx, name, ip, VBAN_PROTOCOL_AUDIO, cb, user);
    if (id >= 0) rx->streams[id].jb = jb;
    return id;
}

void vban_rx_unsubscribe(vban_rx_t *rx, int id)
{
    if (id >= 0 && id < VBAN_RX_MAX_STREAMS) rx->streams[id].active = 0;
}

/* Rate limit: per source IP (one reply per VBAN_PING_PER_SOURCE_MS), a global
 * bucket, and a smaller bucket for public sources so a spoofed internet flood
 * cannot starve LAN discovery. */
static int ping_allowed(vban_rx_t *rx, const vban_addr_t *from, int is_public, uint64_t now)
{
    unsigned i, slot = 0;
    const uint64_t gap = (uint64_t)VBAN_PING_PER_SOURCE_MS * 1000000ull;
    if (now - rx->ping_refill_ns >= 1000000000ull) {
        rx->ping_refill_ns = now;
        rx->ping_tokens = VBAN_PING_REPLY_RATE;
        rx->ping_tokens_public = VBAN_PING_REPLY_RATE_PUBLIC;
    }
    for (i = 0; i < 8; ++i) {
        if (rx->ping_src_ip[i] == from->ip && rx->ping_src_ns[i]) {
            if (now - rx->ping_src_ns[i] < gap) return 0;
            slot = i;
            break;
        }
        if (rx->ping_src_ns[i] < rx->ping_src_ns[slot]) slot = i; /* oldest */
    }
    if (rx->ping_tokens == 0 || (is_public && rx->ping_tokens_public == 0)) return 0;
    rx->ping_tokens--;
    if (is_public) rx->ping_tokens_public--;
    rx->ping_src_ip[slot] = from->ip;
    rx->ping_src_ns[slot] = now;
    return 1;
}

static int handle_service(vban_rx_t *rx, const vban_packet_t *p, const vban_addr_t *from, uint64_t now)
{
    if (vban_service_type(p) == VBAN_SERVICE_IDENTIFICATION &&
        vban_service_function(p) == VBAN_SERVICE_FNCT_PING0 && !vban_service_is_reply(p)) {
        if (rx->cfg.identity && rx->cfg.send) {
            int is_public = !vban_addr_is_private(from->ip);
            if (!vban_addr_is_unicast(from)) {
                rx->stats.pings_refused++;      /* never reflect to broadcast / multicast / port 0 */
            } else if (ping_allowed(rx, from, is_public, now ? now : vban_time_ns() | 1)) {
                uint8_t pkt[VBAN_PING0_PACKET_SIZE];
                vban_name_t label;
                /* identity body only where it does not amplify a spoofable request */
                int full = !is_public || rx->cfg.ping_full_to_public || p->data_len >= VBAN_PING0_SIZE;
                int n;
                memcpy(label.b, p->name, VBAN_STREAM_NAME_SIZE); /* echo request label */
                n = vban_build_ping(pkt, sizeof pkt, p->nu_frame, 1, &label, full ? rx->cfg.identity : NULL);
                if (n > 0 && rx->cfg.send(rx->cfg.send_ctx, pkt, (size_t)n, from) >= 0) rx->stats.pings_answered++;
            } else {
                rx->stats.pings_limited++;
            }
        }
        /* a request carrying a full PING0 is also a device announcement */
        if (p->data_len >= VBAN_PING0_SIZE && rx->cfg.on_service) rx->cfg.on_service(rx->cfg.user, p, from);
        return 1;
    }
    if (rx->cfg.on_service) rx->cfg.on_service(rx->cfg.user, p, from);
    return 1;
}

int vban_rx_process(vban_rx_t *rx, const uint8_t *buf, size_t len, const vban_addr_t *from, uint64_t now)
{
    vban_packet_t p;
    int r = vban_parse(buf, len, &p, rx->cfg.parse_flags), i, taken = 0;
    static const vban_addr_t k_none = { 0, 0 };
    if (!from) from = &k_none;
    if (r != VBAN_OK && r != VBAN_E_UNSUPPORTED) {
        rx->stats.invalid++;
        return r;
    }
    rx->stats.packets++;

    if (p.protocol == VBAN_PROTOCOL_SERVICE) {
        handle_service(rx, &p, from, now);
        return 0;
    }

    for (i = 0; i < VBAN_RX_MAX_STREAMS; ++i) {
        vban_rx_stream_t *s = &rx->streams[i];
        if (!s->active) continue;
        if (s->protocol != VBAN_ANY_PROTOCOL && s->protocol != p.protocol) continue;
        if (s->ip && s->ip != from->ip) continue;
        if (memcmp(s->name.b, k_any_name.b, VBAN_STREAM_NAME_SIZE) != 0) {
            if (s->nocase ? !vban_name_eq_nocase(&s->name, p.name) : !vban_name_eq(&s->name, p.name))
                continue;
        }
        s->packets++;
        if (s->jb && r == VBAN_OK && p.protocol == VBAN_PROTOCOL_AUDIO) vban_jb_push(s->jb, &p);
        if (s->cb) s->cb(s->user, &p, from);
        ++taken;
    }
    if (!taken) {
        rx->stats.unmatched++;
        if (rx->cfg.on_unmatched) rx->cfg.on_unmatched(rx->cfg.user, &p, from);
    }
    return taken;
}

/* ======================================================== reassembly */

void vban_reasm_init(vban_reasm_t *r, void *buf, size_t cap)
{
    memset(r, 0, sizeof *r);
    r->buf = (uint8_t *)buf;
    r->cap = cap;
}

static int append(vban_reasm_t *r, const vban_packet_t *p)
{
    if (r->len + p->data_len > r->cap) {
        r->active = 0;
        r->dropped++;
        return VBAN_E_NOSPACE;
    }
    memcpy(r->buf + r->len, p->data, p->data_len);
    r->len += p->data_len;
    return VBAN_OK;
}

int vban_reasm_frame(vban_reasm_t *r, const vban_packet_t *p)
{
    unsigned type = p->format_bit & 0x07;
    uint16_t idx = (uint16_t)(p->format_nbs | (p->format_nbc << 8));
    if (p->protocol != VBAN_PROTOCOL_FRAME) return VBAN_E_ARG;
    if (type & VBAN_FRAME_START) {
        if (r->active) r->dropped++;
        r->active = 1;
        r->len = 0;
        r->frame = p->nu_frame;
        r->next = 0;
    }
    if (!r->active) return VBAN_E_FORMAT;
    if (p->nu_frame != r->frame || idx != r->next) { /* missing packet: frame is lost */
        r->active = 0;
        r->dropped++;
        return VBAN_E_FORMAT;
    }
    if (append(r, p) != VBAN_OK) return VBAN_E_NOSPACE;
    r->next = (uint16_t)(r->next + 1);
    if (type & VBAN_FRAME_END) {
        r->active = 0;
        return 1;
    }
    return 0;
}

int vban_reasm_serial(vban_reasm_t *r, const vban_packet_t *p, const uint8_t **data, size_t *len)
{
    int more = (p->format_nbs & VBAN_SERIAL_MULTIPART) != 0;
    if (p->protocol != VBAN_PROTOCOL_SERIAL) return VBAN_E_ARG;
    if (r->active && p->nu_frame != r->frame + 1) { /* lost a fragment */
        r->active = 0;
        r->dropped++;
    }
    if (!r->active && !more) { /* single-packet block: zero copy */
        *data = p->data;
        *len = p->data_len;
        return 1;
    }
    if (!r->active) {
        r->active = 1;
        r->len = 0;
    }
    r->frame = p->nu_frame;
    if (append(r, p) != VBAN_OK) return VBAN_E_NOSPACE;
    if (more) return 0;
    r->active = 0;
    *data = r->buf;
    *len = r->len;
    return 1;
}

/* ======================================================== RT-Packet server */

static void mask_set(uint32_t m[4], unsigned id) { m[(id >> 5) & 3] |= 1u << (id & 31); }
static int mask_has(const uint32_t m[4], unsigned id) { return (m[(id >> 5) & 3] >> (id & 31)) & 1u; }

void vban_rtp_server_init(vban_rtp_server_t *s, vban_rtp_client_t *slots, unsigned n,
                          const char *name, vban_send_fn send, void *ctx)
{
    memset(s, 0, sizeof *s);
    memset(slots, 0, sizeof(*slots) * n);
    s->clients = slots;
    s->n_clients = n;
    vban_name_set(&s->name, name ? name : "VBAN RTP");
    s->send = send;
    s->send_ctx = ctx;
}

void vban_rtp_server_offer(vban_rtp_server_t *s, unsigned id)
{
    if (id < 128) mask_set(s->offered, id);
}

int vban_rtp_server_handle(vban_rtp_server_t *s, const vban_packet_t *p, const vban_addr_t *from, uint64_t now)
{
    unsigned id, i, code;
    uint32_t want[4] = { 0, 0, 0, 0 };
    vban_rtp_client_t *c = NULL, *free_slot = NULL;
    uint8_t pkt[VBAN_HEADER_SIZE];
    int any_offered = 0;
    if (p->protocol != VBAN_PROTOCOL_SERVICE || vban_service_type(p) != VBAN_SERVICE_RTPACKETREGISTER ||
        vban_service_is_reply(p))
        return 0;

    /* anti-reflection: an unauthenticated register must not aim a stream at
     * an address that cannot be a real client, nor (by default) at the internet */
    if (!vban_addr_is_unicast(from) || (!s->allow_public && !vban_addr_is_private(from->ip))) {
        s->refused++;
        return 1; /* handled: silently ignored, no reply */
    }

    id = vban_service_function(p);
    mask_set(want, id);
    for (i = 0; i < p->data_len; ++i) mask_set(want, p->data[i] & 0x7F);
    for (i = 0; i < 4; ++i) {
        want[i] &= s->offered[i];
        if (want[i]) any_offered = 1;
    }

    {
        unsigned same_ip = 0;
        for (i = 0; i < s->n_clients; ++i) {
            vban_rtp_client_t *k = &s->clients[i];
            if (k->expire_ns && k->expire_ns < now) k->expire_ns = 0;
            if (k->expire_ns && vban_addr_eq(&k->addr, from)) c = k;
            else if (!k->expire_ns && !free_slot) free_slot = k;
            if (k->expire_ns && k->addr.ip == from->ip) ++same_ip;
        }
        if (!c && same_ip >= VBAN_RTP_MAX_PER_IP) free_slot = NULL; /* one host can not fill the table */
    }
    if (!any_offered) {
        code = VBAN_RTREG_NONE;
    } else if (!c && !free_slot) {
        code = VBAN_RTREG_BUSY;
    } else {
        if (!c) {
            c = free_slot;
            memset(c, 0, sizeof *c);
            c->addr = *from;
        }
        for (i = 0; i < 4; ++i) c->ids[i] = want[i];
        {
            unsigned t = p->format_bit > VBAN_RTP_MAX_TIMEOUT_S ? VBAN_RTP_MAX_TIMEOUT_S : p->format_bit;
            c->expire_ns = now + (uint64_t)t * 1000000000ull + 1; /* t == 0: unsubscribe (spec) */
        }
        code = VBAN_RTREG_OK;
    }
    if (s->send) {
        vban_build_rt_register_reply(pkt, sizeof pkt, &s->name, p->nu_frame, id, code);
        s->send(s->send_ctx, pkt, sizeof pkt, from);
    }
    return 1;
}

int vban_rtp_server_publish(vban_rtp_server_t *s, unsigned id, const void *data, size_t len, uint64_t now)
{
    uint8_t pkt[VBAN_PACKET_MAX_SIZE];
    int n, sent = 0;
    unsigned i;
    if (id > 127 || !s->send) return VBAN_E_ARG;
    n = vban_build_rt_packet(pkt, sizeof pkt, &s->name, s->frame, id, data, len);
    if (n < 0) return n;
    for (i = 0; i < s->n_clients; ++i) {
        vban_rtp_client_t *c = &s->clients[i];
        if (!c->expire_ns) continue;
        if (c->expire_ns < now) { c->expire_ns = 0; continue; }
        if (!mask_has(c->ids, id)) continue;
        if (s->send(s->send_ctx, pkt, (size_t)n, &c->addr) >= 0) ++sent;
    }
    s->frame++;
    return sent;
}

/* ======================================================== streams heard */

int vban_heard_update(vban_heard_t *tab, unsigned n, const vban_packet_t *p, const vban_addr_t *from, uint64_t now)
{
    unsigned i, oldest = 0;
    if (!tab || n == 0 || !p || !from) return VBAN_E_ARG;
    for (i = 0; i < n; ++i) {
        vban_heard_t *h = &tab[i];
        if (h->packets && h->protocol == p->protocol && vban_addr_eq(&h->from, from) &&
            memcmp(h->name.b, p->name, VBAN_STREAM_NAME_SIZE) == 0)
            break;
        if (!h->packets) { oldest = i; break; }
        if (h->last_ns < tab[oldest].last_ns) oldest = i;
    }
    if (i == n || !tab[i].packets) {
        i = (i == n) ? oldest : i;
        memset(&tab[i], 0, sizeof tab[i]);
        tab[i].from = *from;
        memcpy(tab[i].name.b, p->name, VBAN_STREAM_NAME_SIZE);
        tab[i].protocol = p->protocol;
    }
    tab[i].format_SR = (uint8_t)(p->sr_index | p->protocol);
    tab[i].format_nbc = p->format_nbc;
    tab[i].format_bit = p->format_bit;
    tab[i].packets++;
    tab[i].last_ns = now;
    return (int)i;
}
