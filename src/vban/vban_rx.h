/*
 * VBAN SDK - receiver: packet demultiplexer and service responder.
 *
 * vban_rx_process() is transport-agnostic: feed it every UDP datagram you
 * receive (from a socket, lwIP callback, Arduino WiFiUDP, pcap...). It
 *  - validates the packet (zero-copy),
 *  - answers PING0 identification requests automatically (rate limited),
 *  - routes the packet to every matching subscription (name + source IP +
 *    sub protocol); audio subscriptions can push straight into a jitter
 *    buffer,
 *  - hands service packets and unmatched streams to callbacks (discovery).
 *
 * Also here: SERIAL multipart / FRAME reassembly, RT-Packet server,
 * "streams heard" table.
 */
#ifndef VBAN_RX_H
#define VBAN_RX_H

#include <stddef.h>
#include <stdint.h>
#include "vban_proto.h"
#include "vban_jitter.h"

#ifdef __cplusplus
extern "C" {
#endif

#define VBAN_ANY_PROTOCOL 0xFF

typedef void (*vban_rx_cb)(void *user, const vban_packet_t *pkt, const vban_addr_t *from);

typedef struct vban_rx_stream {
    vban_name_t name;       /* all zero = any name */
    uint32_t    ip;         /* 0 = any source */
    uint8_t     protocol;   /* VBAN_PROTOCOL_* or VBAN_ANY_PROTOCOL */
    uint8_t     active;
    uint8_t     nocase;     /* case-insensitive name match */
    vban_jb_t  *jb;         /* optional: audio is pushed here */
    vban_rx_cb  cb;         /* optional */
    void       *user;
    uint32_t    packets;
} vban_rx_stream_t;

typedef struct vban_rx_config {
    const vban_ping0_t *identity;  /* non-NULL: answer PING0 requests with this */
    vban_send_fn send;             /* needed for PING0 / RT replies */
    void        *send_ctx;
    vban_rx_cb   on_service;       /* service packets other than answered pings */
    vban_rx_cb   on_unmatched;     /* valid packets no subscription took */
    void        *user;
    unsigned     parse_flags;      /* VBAN_PARSE_* */
    /* PING0 replies carry the 676-byte identity only to private-network
     * sources or when the request itself was full size (no amplification).
     * Set to 1 to also send it to public sources asking with a bare header. */
    uint8_t      ping_full_to_public;
} vban_rx_config_t;

typedef struct vban_rx_stats {
    uint32_t packets, invalid, unmatched, pings_answered, pings_limited;
    uint32_t pings_refused;        /* from port 0 / broadcast / multicast sources */
} vban_rx_stats_t;

typedef struct vban_rx {
    vban_rx_stream_t streams[VBAN_RX_MAX_STREAMS];
    vban_rx_config_t cfg;
    uint32_t         ping_tokens, ping_tokens_public;
    uint64_t         ping_refill_ns;
    uint32_t         ping_src_ip[8];     /* recent reply destinations (per-source limit) */
    uint64_t         ping_src_ns[8];
    vban_rx_stats_t  stats;
} vban_rx_t;

void vban_rx_init(vban_rx_t *rx, const vban_rx_config_t *cfg);

/* Subscribe. name NULL/"" = any, ip 0 = any. Returns slot id or VBAN_E_NOSPACE. */
int  vban_rx_subscribe(vban_rx_t *rx, const char *name, uint32_t ip, uint8_t protocol,
                       vban_rx_cb cb, void *user);
/* Audio subscription feeding a jitter buffer (cb optional, e.g. for meters). */
int  vban_rx_subscribe_audio(vban_rx_t *rx, const char *name, uint32_t ip, vban_jb_t *jb,
                             vban_rx_cb cb, void *user);
void vban_rx_unsubscribe(vban_rx_t *rx, int id);

/* Process one datagram. now_ns is used for ping rate limiting (pass
 * vban_time_ns(); 0 = read the clock here, limiting can not be disabled). Returns number of subscriptions
 * that took the packet, or a negative parse error. */
int  vban_rx_process(vban_rx_t *rx, const uint8_t *buf, size_t len, const vban_addr_t *from,
                     uint64_t now_ns);

/* -------------------------------------------------- reassembly */

typedef struct vban_reasm {
    uint8_t *buf;
    size_t   cap, len;
    uint32_t frame;      /* frame counter of the frame / last packet */
    uint16_t next;       /* next FRAME packet index */
    uint8_t  active;
    uint32_t dropped;    /* incomplete frames / blocks discarded */
} vban_reasm_t;

void vban_reasm_init(vban_reasm_t *r, void *buf, size_t cap);

/* FRAME sub protocol. Returns 1 when a frame is complete (in r->buf / r->len),
 * 0 if more packets are needed, <0 if the packet was dropped. */
int vban_reasm_frame(vban_reasm_t *r, const vban_packet_t *p);

/* SERIAL multipart blocks (format_nbs bit 7 = more packets follow). Returns 1
 * with data and len set when a block is complete (points into the packet for
 * single-packet blocks: zero copy), 0 if pending, <0 if dropped. */
int vban_reasm_serial(vban_reasm_t *r, const vban_packet_t *p, const uint8_t **data, size_t *len);

/* -------------------------------------------------- RT-Packet server */

typedef struct vban_rtp_client {
    vban_addr_t addr;
    uint64_t    expire_ns;   /* 0 = free slot */
    uint32_t    ids[4];      /* 128-bit mask of subscribed packet ids */
} vban_rtp_client_t;

typedef struct vban_rtp_server {
    vban_rtp_client_t *clients;
    unsigned           n_clients;
    uint32_t           offered[4];  /* packet ids this server provides */
    uint8_t            allow_public; /* accept registrations from public IPs (default 0) */
    uint32_t           refused;      /* registrations ignored (bad / public source) */
    vban_name_t        name;        /* e.g. "Voicemeeter RTP" */
    uint32_t           frame;
    vban_send_fn       send;
    void              *send_ctx;
} vban_rtp_server_t;

void vban_rtp_server_init(vban_rtp_server_t *s, vban_rtp_client_t *slots, unsigned n,
                          const char *name, vban_send_fn send, void *ctx);
void vban_rtp_server_offer(vban_rtp_server_t *s, unsigned packet_id);
/* Feed RTPACKETREGISTER requests; replies are sent. Returns 1 if handled.
 * Anti-reflection (VBAN cannot authenticate a source, so a spoofed register
 * would aim the RT stream at a victim): sources that are not unicast are
 * ignored; public sources are ignored unless allow_public is set; the timeout
 * is capped at VBAN_RTP_MAX_TIMEOUT_S; one IP holds at most
 * VBAN_RTP_MAX_PER_IP slots. Timeout 0 unsubscribes (spec), so a spoofer on
 * the LAN can still cancel a subscription: only secure mode prevents that. */
int  vban_rtp_server_handle(vban_rtp_server_t *s, const vban_packet_t *p, const vban_addr_t *from,
                            uint64_t now_ns);
/* Send packet `id` to every live subscriber. Returns clients reached. */
int  vban_rtp_server_publish(vban_rtp_server_t *s, unsigned packet_id, const void *data, size_t len,
                             uint64_t now_ns);

/* -------------------------------------------------- streams heard */

typedef struct vban_heard {
    vban_addr_t from;
    vban_name_t name;
    uint8_t     protocol, format_SR, format_nbc, format_bit;
    uint32_t    packets;
    uint64_t    last_ns;
} vban_heard_t;

/* Insert/refresh an entry (LRU replacement when full). Returns index, or
 * VBAN_E_ARG for an empty table. */
int vban_heard_update(vban_heard_t *tab, unsigned n, const vban_packet_t *p, const vban_addr_t *from,
                      uint64_t now_ns);

#ifdef __cplusplus
}
#endif
#endif /* VBAN_RX_H */
