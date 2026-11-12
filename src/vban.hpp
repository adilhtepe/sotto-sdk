/*
 * VBAN SDK - header-only C++11 RAII wrapper over the C API.
 * The C structs stay accessible (.c()) for anything not wrapped here.
 */
#ifndef VBAN_HPP
#define VBAN_HPP

#include "vban.h"
#include <cstring>
#include <stdexcept>
#include <string>
#include <vector>

namespace vban {

struct Error : std::runtime_error {
    int code;
    explicit Error(int c) : std::runtime_error(vban_strerror(c)), code(c) {}
};
inline int check(int r) { if (r < 0) throw Error(r); return r; }

using Addr = vban_addr_t;
inline Addr addr(const std::string &host, uint16_t port = VBAN_DEFAULT_PORT)
{
    Addr a;
    check(vban_addr_parse(host.c_str(), port, &a));
    return a;
}

#if VBAN_ENABLE_NET
class Net {
public:
    Net() { check(vban_net_init()); }
    ~Net() { vban_net_cleanup(); }
    Net(const Net &) = delete;
    Net &operator=(const Net &) = delete;
};

class Socket {
public:
    Socket() { s_.fd = -1; }
    Socket(uint16_t port, unsigned flags = VBAN_SOCK_REUSE | VBAN_SOCK_BROADCAST | VBAN_SOCK_QOS,
           const char *bind_ip = nullptr)
    { s_.fd = -1; check(vban_udp_open(&s_, bind_ip, port, flags)); }
    ~Socket() { vban_udp_close(&s_); }
    Socket(Socket &&o) noexcept : s_(o.s_) { o.s_.fd = -1; }
    Socket &operator=(Socket &&o) noexcept { if (this != &o) { vban_udp_close(&s_); s_ = o.s_; o.s_.fd = -1; } return *this; }
    Socket(const Socket &) = delete;
    Socket &operator=(const Socket &) = delete;

    int send(const void *p, size_t n, const Addr &to) { return vban_udp_send(&s_, p, n, &to); }
    int recv(void *p, size_t cap, Addr *from, int timeout_ms) { return vban_udp_recv(&s_, p, cap, from, timeout_ms); }
    vban_socket_t *c() { return &s_; }
    static int sink(void *ctx, const uint8_t *p, size_t n, const vban_addr_t *to)
    { return vban_udp_send(static_cast<vban_socket_t *>(ctx), p, n, to); }
private:
    vban_socket_t s_;
};
#endif

class JitterBuffer {
public:
    explicit JitterBuffer(uint16_t channels, uint32_t capacity_frames = 8192, vban_jb_config_t cfg = vban_jb_config_t())
    {
        if (!cfg.channels) { cfg.channels = channels; cfg.adaptive = 1; }
        mem_.resize(vban_jb_storage_bytes(capacity_frames, cfg.channels, cfg.storage) / 4 + 1);
        check(vban_jb_init(&jb_, &cfg, mem_.data(), capacity_frames));
    }
    JitterBuffer(const JitterBuffer &) = delete;
    JitterBuffer &operator=(const JitterBuffer &) = delete;
    int push(const vban_packet_t &p) { return vban_jb_push(&jb_, &p); }
    size_t pull(float *out, size_t frames) { return vban_jb_pull_f32(&jb_, out, frames); }
    size_t pull(int16_t *out, size_t frames) { return vban_jb_pull_s16(&jb_, out, frames); }
    vban_jb_stats_t stats() const { vban_jb_stats_t s; vban_jb_get_stats(&jb_, &s); return s; }
    uint32_t latency_us() const { return vban_jb_latency_us(&jb_); }
    vban_jb_t *c() { return &jb_; }
private:
    vban_jb_t jb_;
    std::vector<uint32_t> mem_;
};

class Mixer {
public:
    Mixer(unsigned n_in, unsigned n_out) : mem_(vban_mixer_mem_size(n_in, n_out) / 4 + 1)
    { if (vban_mixer_init(&mx_, mem_.data(), mem_.size() * 4, n_in, n_out) != 0) throw Error(VBAN_E_ARG); }
    Mixer(const Mixer &) = delete;
    Mixer &operator=(const Mixer &) = delete;
    void route(unsigned i, unsigned o, float g) { vban_mixer_set_route(&mx_, i, o, g); }
    void input_gain(unsigned i, float g) { vban_mixer_set_input_gain(&mx_, i, g); }
    void output_gain(unsigned o, float g) { vban_mixer_set_output_gain(&mx_, o, g); }
    void master(float g, bool mute = false) { vban_mixer_set_master(&mx_, g, mute); }
    void limiter(int mode) { vban_mixer_set_limiter(&mx_, mode); }
    void process(const float *const *in, float *const *out, size_t n) { vban_mixer_process(&mx_, in, out, n); }
    void process_interleaved(const float *in, float *out, size_t n) { vban_mixer_process_interleaved(&mx_, in, out, n); }
    float take_peak(unsigned o) { return vban_mixer_take_peak(&mx_, o); }
    vban_mixer_t *c() { return &mx_; }
private:
    vban_mixer_t mx_;
    std::vector<uint32_t> mem_;
};

class Sender {
public:
    Sender(const std::string &name, uint32_t rate, uint16_t channels, uint8_t datatype,
           vban_send_fn send, void *ctx, uint16_t frames_per_packet = 0)
    {
        vban_tx_config_t c;
        std::memset(&c, 0, sizeof c);
        c.name = name.c_str(); c.sample_rate = rate; c.channels = channels; c.datatype = datatype;
        c.frames_per_packet = frames_per_packet; c.send = send; c.send_ctx = ctx;
        check(vban_tx_init(&tx_, &c));
    }
    void add_dest(const Addr &a) { check(vban_tx_add_dest(&tx_, &a)); }
    int write(const float *in, size_t frames) { return check(vban_tx_write_f32(&tx_, in, frames)); }
    int write(const int16_t *in, size_t frames) { return check(vban_tx_write_s16(&tx_, in, frames)); }
    int flush() { return vban_tx_flush(&tx_); }
    vban_tx_t *c() { return &tx_; }
private:
    vban_tx_t tx_;
};

class Receiver {
public:
    explicit Receiver(const vban_rx_config_t *cfg = nullptr) { vban_rx_init(&rx_, cfg); }
    int subscribe_audio(const std::string &name, JitterBuffer &jb, uint32_t ip = 0)
    { return check(vban_rx_subscribe_audio(&rx_, name.c_str(), ip, jb.c(), nullptr, nullptr)); }
    int subscribe(const std::string &name, uint8_t protocol, vban_rx_cb cb, void *user, uint32_t ip = 0)
    { return check(vban_rx_subscribe(&rx_, name.c_str(), ip, protocol, cb, user)); }
    int process(const uint8_t *p, size_t n, const Addr &from, uint64_t now_ns = 0)
    { return vban_rx_process(&rx_, p, n, &from, now_ns); }
    vban_rx_t *c() { return &rx_; }
private:
    vban_rx_t rx_;
};

} // namespace vban

#endif /* VBAN_HPP */
