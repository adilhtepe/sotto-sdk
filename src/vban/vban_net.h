/*
 * VBAN SDK - thin UDP / clock / thread-priority layer.
 *
 * Backends: POSIX (Linux, Android, macOS, iOS, *BSD, ESP-IDF lwIP, Zephyr
 * with POSIX sockets) and Win32 (Winsock2). On Linux, batches use
 * recvmmsg/sendmmsg (one syscall for up to VBAN_NET_BATCH packets).
 * Compile with VBAN_ENABLE_NET=0 to drop this module and feed packets from
 * your own transport (Arduino WiFiUDP, RTOS stacks, DPDK, ...).
 *
 * Addresses are IPv4 (the VBAN ecosystem is IPv4-only); ip is host order.
 */
#ifndef VBAN_NET_H
#define VBAN_NET_H

#include <stddef.h>
#include <stdint.h>
#include "vban_proto.h"

#ifdef __cplusplus
extern "C" {
#endif

int  vban_addr_parse(const char *host, uint16_t port, vban_addr_t *out); /* "1.2.3.4" or hostname */
void vban_addr_format(const vban_addr_t *a, char out[22]);              /* "a.b.c.d:port" */

#if VBAN_ENABLE_NET

typedef struct vban_socket { intptr_t fd; } vban_socket_t;
#define VBAN_SOCKET_INIT { -1 }

/* open flags */
#define VBAN_SOCK_BROADCAST 0x01  /* allow sending to broadcast addresses */
#define VBAN_SOCK_REUSE     0x02  /* SO_REUSEADDR (+SO_REUSEPORT where available) */
#define VBAN_SOCK_NONBLOCK  0x04
#define VBAN_SOCK_QOS       0x08  /* DSCP EF (46) + SO_PRIORITY 6: voice-class queuing */

/* Call once per process (WSAStartup on Windows; no-op elsewhere). */
int  vban_net_init(void);
void vban_net_cleanup(void);

/* bind_ip NULL/"0.0.0.0" = any; port 0 = ephemeral (send-only sockets). */
int  vban_udp_open(vban_socket_t *s, const char *bind_ip, uint16_t port, unsigned flags);
void vban_udp_close(vban_socket_t *s);
int  vban_udp_set_buffers(vban_socket_t *s, int rcvbuf, int sndbuf);
int  vban_udp_join_multicast(vban_socket_t *s, uint32_t group_ip, uint32_t iface_ip);
int  vban_udp_local_port(const vban_socket_t *s);

int  vban_udp_send(vban_socket_t *s, const void *buf, size_t len, const vban_addr_t *to);
/* Returns bytes received, VBAN_E_TIMEOUT, VBAN_E_NET, or VBAN_E_OVERSIZE for a
 * datagram larger than `cap` (dropped, never returned cut). timeout_ms < 0 = block. */
int  vban_udp_recv(vban_socket_t *s, void *buf, size_t cap, vban_addr_t *from, int timeout_ms);
/* 1 if readable, 0 on timeout, <0 on error. */
int  vban_udp_wait(vban_socket_t *s, int timeout_ms);
/* Same with microsecond resolution (use this to wait for sub-ms audio
 * deadlines instead of polling with a 0 timeout). timeout_us < 0 = block. */
int  vban_udp_wait_us(vban_socket_t *s, long timeout_us);

typedef struct vban_msg {
    uint8_t    *buf;
    uint16_t    cap;   /* buffer size (recv) */
    uint16_t    len;   /* bytes (recv: filled in, send: to send) */
    vban_addr_t addr;  /* recv: source, send: destination */
} vban_msg_t;

/* Receive up to n packets; waits up to timeout_ms for the first one only.
 * Returns count (>0), VBAN_E_TIMEOUT or VBAN_E_NET. A datagram larger than its
 * buffer comes back with len 0 (dropped; vban_rx_process rejects it). */
int  vban_udp_recv_batch(vban_socket_t *s, vban_msg_t *msgs, unsigned n, int timeout_ms);
int  vban_udp_recv_batch_us(vban_socket_t *s, vban_msg_t *msgs, unsigned n, long timeout_us);
/* Send n packets; returns number sent or VBAN_E_NET. */
int  vban_udp_send_batch(vban_socket_t *s, const vban_msg_t *msgs, unsigned n);

/* vban_send_fn adapter: pass the vban_socket_t* as ctx. */
int  vban_udp_sink(void *socket, const uint8_t *pkt, size_t len, const vban_addr_t *to);

#endif /* VBAN_ENABLE_NET */

/* Monotonic clock in nanoseconds. */
uint64_t vban_time_ns(void);
/* Sleep until an absolute vban_time_ns() deadline (sub-ms where the OS allows). */
void     vban_sleep_until_ns(uint64_t deadline);
/* Best-effort real-time priority for the calling thread (SCHED_FIFO, MMCSS
 * "Pro Audio", QoS user-interactive). Returns 0 on success. */
int      vban_thread_realtime(void);

#ifdef __cplusplus
}
#endif
#endif /* VBAN_NET_H */
