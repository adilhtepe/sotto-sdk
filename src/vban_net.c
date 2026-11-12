/* VBAN SDK - UDP / clock / thread layer. See vban/vban_net.h. */
#if defined(__linux__) && !defined(_GNU_SOURCE)
#define _GNU_SOURCE 1 /* recvmmsg / sendmmsg / clock_nanosleep */
#endif
#if !defined(_WIN32) && !defined(_POSIX_C_SOURCE) && !defined(__APPLE__) && !defined(_GNU_SOURCE)
#define _POSIX_C_SOURCE 200809L
#endif

#include "vban/vban_net.h"
#include <stdio.h>
#include <string.h>

#if defined(_WIN32)
/* ============================================================== Win32 */
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <winsock2.h>
#include <ws2tcpip.h>
#include <windows.h>
#ifdef _MSC_VER
#pragma comment(lib, "ws2_32.lib")
#endif
/* socklen_t comes from ws2tcpip.h (MSVC and MinGW-w64 both define it). */
#define CLOSESOCK closesocket
#ifndef SIO_UDP_CONNRESET
#define SIO_UDP_CONNRESET _WSAIOW(IOC_VENDOR, 12)
#endif
#elif defined(ARDUINO) && !VBAN_ENABLE_NET && !defined(VBAN_CUSTOM_CLOCK)
/* Arduino without sockets: clock from the core (C linkage in every core). */
#define VBAN_ARDUINO_CLOCK 1
extern unsigned long micros(void);
#elif VBAN_ENABLE_NET || !defined(VBAN_CUSTOM_CLOCK)
/* ============================================================== POSIX */
#include <errno.h>
#include <fcntl.h>
#include <netdb.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <time.h>
#include <unistd.h>
#if defined(ESP_PLATFORM)
#include <sys/select.h>
#define VBAN_USE_SELECT 1
#else
#include <poll.h>
#include <netinet/ip.h>
#include <pthread.h>
#include <sched.h>
#if defined(__ANDROID__)
#include <sys/resource.h>
#endif
#endif
#if defined(__APPLE__)
#include <mach/mach_time.h>
#include <pthread/qos.h>
#endif
#define CLOSESOCK close
#endif

/* ------------------------------------------------------------ addresses */

int vban_addr_parse(const char *host, uint16_t port, vban_addr_t *out)
{
#if VBAN_ENABLE_NET
    struct in_addr a;
    if (!host || !out) return VBAN_E_ARG;
    out->port = port;
    if (inet_pton(AF_INET, host, &a) == 1) {
        out->ip = ntohl(a.s_addr);
        return VBAN_OK;
    }
    {
        struct addrinfo hints, *res = NULL;
        memset(&hints, 0, sizeof hints);
        hints.ai_family = AF_INET;
        hints.ai_socktype = SOCK_DGRAM;
        if (getaddrinfo(host, NULL, &hints, &res) != 0 || !res) return VBAN_E_NOTFOUND;
        out->ip = ntohl(((struct sockaddr_in *)res->ai_addr)->sin_addr.s_addr);
        freeaddrinfo(res);
        return VBAN_OK;
    }
#else
    unsigned a, b, c, d;
    if (!host || !out || sscanf(host, "%u.%u.%u.%u", &a, &b, &c, &d) != 4 ||
        a > 255 || b > 255 || c > 255 || d > 255)
        return VBAN_E_ARG;
    out->ip = VBAN_IP(a, b, c, d);
    out->port = port;
    return VBAN_OK;
#endif
}

void vban_addr_format(const vban_addr_t *a, char out[22])
{
    snprintf(out, 22, "%u.%u.%u.%u:%u", (unsigned)(a->ip >> 24), (unsigned)(a->ip >> 16) & 255,
             (unsigned)(a->ip >> 8) & 255, (unsigned)a->ip & 255, (unsigned)a->port);
}

#if VBAN_ENABLE_NET

static void to_sa(const vban_addr_t *a, struct sockaddr_in *sa)
{
    memset(sa, 0, sizeof *sa);
    sa->sin_family = AF_INET;
    sa->sin_port = htons(a->port);
    sa->sin_addr.s_addr = htonl(a->ip);
}

static void from_sa(const struct sockaddr_in *sa, vban_addr_t *a)
{
    a->ip = ntohl(sa->sin_addr.s_addr);
    a->port = ntohs(sa->sin_port);
}

/* ------------------------------------------------------------ lifecycle */

int vban_net_init(void)
{
#ifdef _WIN32
    WSADATA wsa;
    return WSAStartup(MAKEWORD(2, 2), &wsa) == 0 ? VBAN_OK : VBAN_E_NET;
#else
    return VBAN_OK;
#endif
}

void vban_net_cleanup(void)
{
#ifdef _WIN32
    WSACleanup();
#endif
}

int vban_udp_open(vban_socket_t *s, const char *bind_ip, uint16_t port, unsigned flags)
{
    struct sockaddr_in sa;
    vban_addr_t a;
    int one = 1;
#ifdef _WIN32
    SOCKET fd;
#else
    int fd;
#endif
    if (!s) return VBAN_E_ARG;
    s->fd = -1;
    a.ip = 0;
    a.port = port;
    if (bind_ip && *bind_ip && vban_addr_parse(bind_ip, port, &a) != VBAN_OK) return VBAN_E_ARG;

    fd = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
#ifdef _WIN32
    if (fd == INVALID_SOCKET) return VBAN_E_NET;
#else
    if (fd < 0) return VBAN_E_NET;
#endif
    if (flags & VBAN_SOCK_REUSE) {
        setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, (const char *)&one, sizeof one);
#if defined(SO_REUSEPORT) && !defined(_WIN32) && !defined(ESP_PLATFORM)
        setsockopt(fd, SOL_SOCKET, SO_REUSEPORT, (const char *)&one, sizeof one);
#endif
    }
    if (flags & VBAN_SOCK_BROADCAST)
        setsockopt(fd, SOL_SOCKET, SO_BROADCAST, (const char *)&one, sizeof one);
    if (flags & VBAN_SOCK_QOS) {
#if defined(IP_TOS)
        int tos = 0xB8; /* DSCP EF */
        setsockopt(fd, IPPROTO_IP, IP_TOS, (const char *)&tos, sizeof tos);
#endif
#if defined(SO_PRIORITY)
        {
            int prio = 6;
            setsockopt(fd, SOL_SOCKET, SO_PRIORITY, (const char *)&prio, sizeof prio);
        }
#endif
    }
#ifdef _WIN32
    {
        /* Stop ICMP "port unreachable" from turning into WSAECONNRESET on
         * the next recvfrom (classic Windows UDP pitfall). */
        BOOL off = FALSE;
        DWORD ret = 0;
        WSAIoctl(fd, SIO_UDP_CONNRESET, &off, sizeof off, NULL, 0, &ret, NULL, NULL);
    }
#endif
    to_sa(&a, &sa);
    if (bind(fd, (struct sockaddr *)&sa, sizeof sa) != 0) {
        CLOSESOCK(fd);
        return VBAN_E_NET;
    }
    if (flags & VBAN_SOCK_NONBLOCK) {
#ifdef _WIN32
        u_long nb = 1;
        ioctlsocket(fd, FIONBIO, &nb);
#else
        fcntl(fd, F_SETFL, fcntl(fd, F_GETFL, 0) | O_NONBLOCK);
#endif
    }
    s->fd = (intptr_t)fd;
    return VBAN_OK;
}

void vban_udp_close(vban_socket_t *s)
{
    if (s && s->fd >= 0) {
        CLOSESOCK(s->fd);
        s->fd = -1;
    }
}

int vban_udp_set_buffers(vban_socket_t *s, int rcv, int snd)
{
    int r = 0;
    if (rcv > 0) r |= setsockopt(s->fd, SOL_SOCKET, SO_RCVBUF, (const char *)&rcv, sizeof rcv);
    if (snd > 0) r |= setsockopt(s->fd, SOL_SOCKET, SO_SNDBUF, (const char *)&snd, sizeof snd);
    return r ? VBAN_E_NET : VBAN_OK;
}

int vban_udp_join_multicast(vban_socket_t *s, uint32_t group, uint32_t iface)
{
    struct ip_mreq m;
    m.imr_multiaddr.s_addr = htonl(group);
    m.imr_interface.s_addr = htonl(iface);
    return setsockopt(s->fd, IPPROTO_IP, IP_ADD_MEMBERSHIP, (const char *)&m, sizeof m) ? VBAN_E_NET : VBAN_OK;
}

int vban_udp_local_port(const vban_socket_t *s)
{
    struct sockaddr_in sa;
    socklen_t l = sizeof sa;
    if (getsockname(s->fd, (struct sockaddr *)&sa, &l) != 0) return VBAN_E_NET;
    return ntohs(sa.sin_port);
}

/* ------------------------------------------------------------ I/O */

int vban_udp_send(vban_socket_t *s, const void *buf, size_t len, const vban_addr_t *to)
{
    struct sockaddr_in sa;
    to_sa(to, &sa);
    return sendto(s->fd, (const char *)buf, (int)len, 0, (struct sockaddr *)&sa, sizeof sa) == (int)len
               ? (int)len : VBAN_E_NET;
}

int vban_udp_sink(void *socket, const uint8_t *pkt, size_t len, const vban_addr_t *to)
{
    return vban_udp_send((vban_socket_t *)socket, pkt, len, to);
}

/* Microsecond-resolution wait. A millisecond API forces callers that wait for
 * a sub-ms deadline to pass 0 and spin: measured 64k empty polls/s and 6 % of
 * a core in a 48 kHz receiver loop before this existed. */
int vban_udp_wait_us(vban_socket_t *s, long timeout_us)
{
#if defined(__linux__)
    struct pollfd p;
    struct timespec ts, *tp = NULL;
    int r;
    p.fd = (int)s->fd; p.events = POLLIN; p.revents = 0;
    if (timeout_us >= 0) { ts.tv_sec = timeout_us / 1000000; ts.tv_nsec = (timeout_us % 1000000) * 1000; tp = &ts; }
    do { r = ppoll(&p, 1, tp, NULL); } while (r < 0 && errno == EINTR);
    return r < 0 ? VBAN_E_NET : r;
#else
    /* select() takes microseconds on Winsock, BSD/macOS and lwIP alike */
    fd_set rs;
    struct timeval tv, *tp = NULL;
    int r;
    FD_ZERO(&rs);
    FD_SET(s->fd, &rs);
    if (timeout_us >= 0) { tv.tv_sec = (long)(timeout_us / 1000000); tv.tv_usec = (long)(timeout_us % 1000000); tp = &tv; }
#if defined(_WIN32)
    r = select(0, &rs, NULL, NULL, tp);
#else
    do { r = select((int)s->fd + 1, &rs, NULL, NULL, tp); } while (r < 0 && errno == EINTR);
#endif
    return r < 0 ? VBAN_E_NET : r;
#endif
}

int vban_udp_wait(vban_socket_t *s, int timeout_ms)
{
    return vban_udp_wait_us(s, timeout_ms < 0 ? -1L : (long)timeout_ms * 1000L);
}

static int would_block(void)
{
#ifdef _WIN32
    int e = WSAGetLastError();
    return e == WSAEWOULDBLOCK || e == WSAECONNRESET;
#else
    return errno == EAGAIN || errno == EWOULDBLOCK || errno == EINTR;
#endif
}

/* Returns bytes, VBAN_E_TIMEOUT, VBAN_E_NET, or VBAN_E_OVERSIZE when the
 * datagram did not fit in `buf` (it was cut and is dropped: a truncated TEXT,
 * SERIAL or SERVICE payload would otherwise parse as a valid shorter one). */
static int recv_one(vban_socket_t *s, void *buf, size_t cap, vban_addr_t *from, int dontwait)
{
    struct sockaddr_in sa;
#ifdef _WIN32
    int l = sizeof sa, n;
    (void)dontwait;
    n = recvfrom((SOCKET)s->fd, (char *)buf, (int)cap, 0, (struct sockaddr *)&sa, &l);
    if (n < 0) {
        if (WSAGetLastError() == WSAEMSGSIZE) return VBAN_E_OVERSIZE;
        return would_block() ? VBAN_E_TIMEOUT : VBAN_E_NET;
    }
#else
    struct iovec iov;
    struct msghdr mh;
    int flags = 0, n;
#if defined(MSG_DONTWAIT)
    if (dontwait) flags = MSG_DONTWAIT;
#else
    (void)dontwait;
#endif
    iov.iov_base = buf;
    iov.iov_len = cap;
    memset(&mh, 0, sizeof mh);
    mh.msg_name = &sa;
    mh.msg_namelen = sizeof sa;
    mh.msg_iov = &iov;
    mh.msg_iovlen = 1;
    n = (int)recvmsg((int)s->fd, &mh, flags);
    if (n < 0) return would_block() ? VBAN_E_TIMEOUT : VBAN_E_NET;
    if (mh.msg_flags & MSG_TRUNC) return VBAN_E_OVERSIZE;
#endif
    if (from) from_sa(&sa, from);
    return n;
}

int vban_udp_recv(vban_socket_t *s, void *buf, size_t cap, vban_addr_t *from, int timeout_ms)
{
    if (timeout_ms >= 0) {
        int w = vban_udp_wait(s, timeout_ms);
        if (w <= 0) return w == 0 ? VBAN_E_TIMEOUT : w;
    }
    return recv_one(s, buf, cap, from, 0);
}

int vban_udp_recv_batch(vban_socket_t *s, vban_msg_t *m, unsigned n, int timeout_ms)
{
    return vban_udp_recv_batch_us(s, m, n, timeout_ms < 0 ? -1L : (long)timeout_ms * 1000L);
}

int vban_udp_recv_batch_us(vban_socket_t *s, vban_msg_t *m, unsigned n, long timeout_us)
{
    unsigned i;
    int w;
    if (n == 0) return 0;
    if (n > VBAN_NET_BATCH) n = VBAN_NET_BATCH;
    if (timeout_us >= 0) {
        w = vban_udp_wait_us(s, timeout_us);
        if (w <= 0) return w == 0 ? VBAN_E_TIMEOUT : w;
    }
#if defined(__linux__) && !defined(VBAN_NO_MMSG)
    {
        struct mmsghdr h[VBAN_NET_BATCH];
        struct iovec iov[VBAN_NET_BATCH];
        struct sockaddr_in sa[VBAN_NET_BATCH];
        int r;
        memset(h, 0, sizeof(h[0]) * n);
        for (i = 0; i < n; ++i) {
            iov[i].iov_base = m[i].buf;
            iov[i].iov_len = m[i].cap;
            h[i].msg_hdr.msg_iov = &iov[i];
            h[i].msg_hdr.msg_iovlen = 1;
            h[i].msg_hdr.msg_name = &sa[i];
            h[i].msg_hdr.msg_namelen = sizeof sa[i];
        }
        do { r = recvmmsg((int)s->fd, h, n, MSG_DONTWAIT, NULL); } while (r < 0 && errno == EINTR);
        if (r < 0) return would_block() ? VBAN_E_TIMEOUT : VBAN_E_NET;
        for (i = 0; i < (unsigned)r; ++i) {
            /* truncated datagram: len 0, so vban_rx_process() rejects it */
            m[i].len = (h[i].msg_hdr.msg_flags & MSG_TRUNC) ? 0
                       : (uint16_t)(h[i].msg_len > 0xFFFF ? 0xFFFF : h[i].msg_len);
            from_sa(&sa[i], &m[i].addr);
        }
        return r;
    }
#else
    for (i = 0; i < n; ++i) {
        int r = recv_one(s, m[i].buf, m[i].cap, &m[i].addr, 1);
        if (r == VBAN_E_OVERSIZE) r = 0;        /* dropped: hand back an empty message */
        if (r < 0) break;
        m[i].len = (uint16_t)r;
#if !defined(MSG_DONTWAIT)
        if (vban_udp_wait(s, 0) <= 0) { ++i; break; }
#endif
    }
    return i ? (int)i : VBAN_E_TIMEOUT;
#endif
}

int vban_udp_send_batch(vban_socket_t *s, const vban_msg_t *m, unsigned n)
{
    unsigned i;
#if defined(__linux__) && !defined(VBAN_NO_MMSG)
    struct mmsghdr h[VBAN_NET_BATCH];
    struct iovec iov[VBAN_NET_BATCH];
    struct sockaddr_in sa[VBAN_NET_BATCH];
    unsigned done = 0;
    while (done < n) {
        unsigned k = n - done > VBAN_NET_BATCH ? VBAN_NET_BATCH : n - done;
        int r;
        memset(h, 0, sizeof(h[0]) * k);
        for (i = 0; i < k; ++i) {
            const vban_msg_t *x = &m[done + i];
            to_sa(&x->addr, &sa[i]);
            iov[i].iov_base = x->buf;
            iov[i].iov_len = x->len;
            h[i].msg_hdr.msg_iov = &iov[i];
            h[i].msg_hdr.msg_iovlen = 1;
            h[i].msg_hdr.msg_name = &sa[i];
            h[i].msg_hdr.msg_namelen = sizeof sa[i];
        }
        do { r = sendmmsg((int)s->fd, h, k, 0); } while (r < 0 && errno == EINTR);
        if (r <= 0) return done ? (int)done : VBAN_E_NET;
        done += (unsigned)r;
    }
    return (int)done;
#else
    for (i = 0; i < n; ++i)
        if (vban_udp_send(s, m[i].buf, m[i].len, &m[i].addr) < 0) return i ? (int)i : VBAN_E_NET;
    return (int)n;
#endif
}

#endif /* VBAN_ENABLE_NET */

/* ------------------------------------------------------------ clock */
#if !defined(VBAN_CUSTOM_CLOCK) /* define it and provide these three yourself on bare metal */

uint64_t vban_time_ns(void)
{
#if defined(VBAN_ARDUINO_CLOCK)
    static uint32_t last, hi;  /* extend the 32-bit micros() counter */
    uint32_t now = (uint32_t)micros();
    if (now < last) ++hi;
    last = now;
    return (((uint64_t)hi << 32) | now) * 1000ull;
#elif defined(_WIN32)
    static uint64_t freq;
    LARGE_INTEGER c;
    uint64_t q;
    if (!freq) {
        LARGE_INTEGER f;
        QueryPerformanceFrequency(&f);
        freq = (uint64_t)f.QuadPart;
    }
    QueryPerformanceCounter(&c);
    q = (uint64_t)c.QuadPart;
    /* Split to stay exact: a double loses ns resolution once the counter
     * exceeds 2^53 ns (~104 days of uptime). */
    return q / freq * 1000000000ull + q % freq * 1000000000ull / freq;
#elif defined(__APPLE__)
    static mach_timebase_info_data_t tb;
    if (!tb.denom) mach_timebase_info(&tb);
    return mach_absolute_time() * tb.numer / tb.denom;
#else
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint64_t)ts.tv_sec * 1000000000ull + (uint64_t)ts.tv_nsec;
#endif
}

void vban_sleep_until_ns(uint64_t deadline)
{
#if defined(VBAN_ARDUINO_CLOCK)
    while (vban_time_ns() < deadline) {}
#elif defined(_WIN32)
    static HANDLE timer;
    uint64_t now = vban_time_ns();
    LARGE_INTEGER due;
    if (deadline <= now) return;
#ifndef CREATE_WAITABLE_TIMER_HIGH_RESOLUTION
#define CREATE_WAITABLE_TIMER_HIGH_RESOLUTION 0x00000002
#endif
    if (!timer) timer = CreateWaitableTimerExW(NULL, NULL, CREATE_WAITABLE_TIMER_HIGH_RESOLUTION, TIMER_ALL_ACCESS);
    if (!timer) timer = CreateWaitableTimerW(NULL, FALSE, NULL);
    due.QuadPart = -(LONGLONG)((deadline - now) / 100); /* relative, 100 ns units */
    if (timer && SetWaitableTimer(timer, &due, 0, NULL, NULL, FALSE))
        WaitForSingleObject(timer, INFINITE);
    while (vban_time_ns() < deadline) YieldProcessor(); /* final sub-tick spin */
#elif defined(__linux__)
    struct timespec ts;
    ts.tv_sec = (time_t)(deadline / 1000000000ull);
    ts.tv_nsec = (long)(deadline % 1000000000ull);
    while (clock_nanosleep(CLOCK_MONOTONIC, TIMER_ABSTIME, &ts, NULL) == EINTR) {}
#elif defined(ESP_PLATFORM)
    /* ESP-IDF's newlib has no nanosleep. Its usleep busy-waits below one
     * FreeRTOS tick and calls vTaskDelay above that. */
    uint64_t now = vban_time_ns();
    if (deadline > now) {
        uint64_t us = (deadline - now) / 1000u;
        for (; us > 999999u; us -= 999999u) usleep(999999u); /* POSIX: arg < 1 s */
        if (us) usleep((useconds_t)us);
    }
#else
    uint64_t now = vban_time_ns();
    if (deadline > now) {
        struct timespec ts;
        uint64_t d = deadline - now;
        ts.tv_sec = (time_t)(d / 1000000000ull);
        ts.tv_nsec = (long)(d % 1000000000ull);
        nanosleep(&ts, NULL);
    }
#endif
}

int vban_thread_realtime(void)
{
#if defined(VBAN_ARDUINO_CLOCK)
    return -1;
#elif defined(_WIN32)
    typedef HANDLE(WINAPI * av_fn)(LPCWSTR, LPDWORD);
    typedef UINT(WINAPI * tbp_fn)(UINT);
    HMODULE avrt = LoadLibraryA("avrt.dll"), winmm = LoadLibraryA("winmm.dll");
    int ok = SetThreadPriority(GetCurrentThread(), THREAD_PRIORITY_TIME_CRITICAL) ? 0 : -1;
    if (winmm) {
        /* Cast through void (*)(void): ISO C has no function <-> void * conversion. */
        tbp_fn tbp = (tbp_fn)(void (*)(void))GetProcAddress(winmm, "timeBeginPeriod");
        if (tbp) tbp(1);
    }
    if (avrt) {
        av_fn av = (av_fn)(void (*)(void))GetProcAddress(avrt, "AvSetMmThreadCharacteristicsW");
        DWORD idx = 0;
        if (av && av(L"Pro Audio", &idx)) ok = 0;
    }
    return ok;
#elif defined(__APPLE__)
    return pthread_set_qos_class_self_np(QOS_CLASS_USER_INTERACTIVE, 0) == 0 ? 0 : -1;
#elif defined(ESP_PLATFORM)
    return -1; /* use FreeRTOS task priorities */
#elif defined(__linux__) || defined(__unix__)
    struct sched_param sp;
    memset(&sp, 0, sizeof sp);
    sp.sched_priority = 70;
    if (pthread_setschedparam(pthread_self(), SCHED_FIFO, &sp) == 0) return 0;
#if defined(__ANDROID__)
    /* Apps get EPERM for SCHED_FIFO; what Android allows is the nice value of
     * android.os.Process.THREAD_PRIORITY_URGENT_AUDIO (-19) on own threads. */
    if (setpriority(PRIO_PROCESS, (id_t)gettid(), -19) == 0) return 0;
#endif
    return -1;
#else
    return -1;
#endif
}
#endif /* VBAN_CUSTOM_CLOCK */
