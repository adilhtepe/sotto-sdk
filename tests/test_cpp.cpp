// C++ wrapper smoke test: the SDK headers compile as C++ and the RAII types work.
#include "vban.hpp"
#include <cstdio>
#include <vector>

struct Cap { std::vector<std::vector<uint8_t> > pkts; };
static int cap_send(void *ctx, const uint8_t *p, size_t n, const vban_addr_t *)
{
    static_cast<Cap *>(ctx)->pkts.push_back(std::vector<uint8_t>(p, p + n));
    return (int)n;
}

int main()
{
    Cap cap;
    vban::Sender tx("cpp", 48000, 2, VBAN_DATATYPE_F32, cap_send, &cap, 128);
    tx.add_dest(vban::addr("127.0.0.1"));
    std::vector<float> in(256 * 2, 0.5f), out(256 * 2);
    tx.write(in.data(), 256);

    vban_jb_config_t cfg = vban_jb_config_t();
    cfg.channels = 2; cfg.drift = VBAN_JB_DRIFT_NONE; cfg.target_frames = 256;
    vban::JitterBuffer jb(2, 1024, cfg);
    vban::Receiver rx;
    rx.subscribe_audio("cpp", jb);
    for (size_t i = 0; i < cap.pkts.size(); ++i) rx.process(cap.pkts[i].data(), cap.pkts[i].size(), vban::addr("127.0.0.1"));
    size_t got = jb.pull(out.data(), 256);

    vban::Mixer mx(2, 2);
    mx.process_interleaved(out.data(), in.data(), 256);
    mx.process_interleaved(out.data(), in.data(), 256);

    bool ok = cap.pkts.size() == 2 && got == 256 && out[511] == 0.5f && in[100] == 0.5f;
    try { vban::addr("not an address at all ..."); ok = false; } catch (const vban::Error &) {}
    std::printf("cpp wrapper: %s\n", ok ? "ok" : "FAIL");
    return ok ? 0 : 1;
}
