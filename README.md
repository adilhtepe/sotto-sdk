# VBAN SDK

A portable C99 SDK for the VB-Audio **VBAN** network protocol (spec revision 13, Sep 2025). You can
use it to build receivers, transmitters, mixers, routers, MIDI/serial bridges, remote controllers and
discovery tools. It has three design goals: **no heap allocation**, **no blocking in the audio path**,
and **byte-exact wire handling on any CPU** (little- or big-endian, any alignment).

```
src/vban.h            umbrella header (C)        src/vban.hpp   header-only C++11 RAII wrapper
src/vban/vban_proto.h wire format, parse/build, PING0, sequencing   (pure C, no OS)
src/vban/vban_pcm.h   u8/s16/s24/s32/f32/f64 conversion, gain, peak (pure C, no OS)
src/vban/vban_jitter.h lock-free jitter buffer, PLC, adaptive latency, drift/SRC (pure C)
src/vban/vban_mixer.h N x M matrix mixer, click-free ramps, limiter (pure C)
src/vban/vban_rx.h    demux, PING0 responder, SERIAL/FRAME reassembly, RT-Packet server
src/vban/vban_tx.h    zero-copy packetizer, TEXT/SERIAL/MIDI/FRAME/PING senders, pacer
src/vban/vban_net.h   UDP (POSIX/Winsock, recvmmsg/sendmmsg), clock, RT thread priority
src/vban/vban_secure.h optional secure VBAN: certificates, Noise XX handshake, encrypted packets
```

## Protocol coverage

| Spec item | Status |
|---|---|
| 28-byte header, `'VBAN'` magic, 1464 B packet / 1436 B data / 256 samples / 256 channels | ✔ enforced |
| AUDIO PCM: all 21 sample rates, u8, s16, s24, s32, f32, f64 | ✔ encode + decode |
| AUDIO 10/12-bit | header accepted, payload passed through (the spec defines no storage layout) |
| AUDIO codecs VBCA / VBCV (proprietary) | detected, reported as `VBAN_E_UNSUPPORTED`, forwardable |
| SERIAL (bps table, bitmode, channel ident, GENERIC / MIDI, multipart bit 7) | ✔ send with auto-split, reassembly |
| TEXT (ASCII / UTF-8 / WCHAR, Voicemeeter `Command1` format) | ✔ |
| FRAME (packet index LSB/MSB, START / CONTINUE / END, mbps table) | ✔ send with split, reassembly with loss detection |
| SERVICE PING0 (676-byte struct, request/reply, id echo) | ✔ build/parse + automatic, rate-limited responder |
| SERVICE RT-Packet register / reply codes / RT packets | ✔ client builder + server with subscription expiry |
| SERVICE REQUESTREPLY (Voicemeeter Matrix), CHATUTF8 | ✔ constants and callbacks |
| Frame counter: loss, duplicate, reorder, wrap, sender restart | ✔ `vban_seq_*`, used by the jitter buffer |

### Interpretations where the spec is silent or ambiguous
These choices follow the reference implementations in the wild (thib3113/vban, vban-cmd, VBAN-Key,
Voicemeeter behaviour) as surveyed for this SDK.

- **RT-Packet register**: the packet id goes in the function field (`format_nbs`), timeout in
  `format_bit`, and an optional id list in the payload. All known Voicemeeter clients do this.
- **SERIAL multipart**: `format_nbs` bit 7 = "more packets follow". The last fragment clears it.
  Fragments must have consecutive frame counters; a gap discards the block.
- **FRAME**: all packets of one frame share the frame counter. A single-packet frame is sent as
  `START|END`.
- **Stream names** compare byte-exact and case-sensitive over 16 bytes. Bytes after the first NUL are
  ignored (some senders leave garbage there). A case-insensitive mode is available per subscription.
- **Parsing is lenient by default.** It only rejects what is unsafe: bad magic, truncation, a PCM size
  mismatch, an undefined rate index, or the reserved bit. `VBAN_PARSE_STRICT` also checks the
  "must be zero" fields, for conformance testing.
- **Endianness**: everything is little-endian on the wire, and the SDK encodes it byte-by-byte. Some
  third-party libraries write `nuFrame` big-endian; that is wrong and is not copied here.

## Building

**CMake** (Linux, macOS, Windows/MSVC/MinGW, Android NDK, iOS):
```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release && cmake --build build -j
```
```bash
ctest --test-dir build
```
```bash
./build/vban_bench
```
Options: `VBAN_BUILD_TESTS`, `VBAN_BUILD_EXAMPLES`, `VBAN_BUILD_BENCH`, `VBAN_ENABLE_NET`, `VBAN_SANITIZE`,
`VBAN_WERROR` (warnings are errors; CI turns it on).

**CI**: `.github/workflows/ci.yml` builds with `VBAN_WERROR=ON` and runs both test programs on Linux
(GCC, Clang+ASan, aarch64/armhf under qemu), macOS (arm64, x86-64), Windows (MSVC x64/x86/ARM64,
clang-cl, MinGW UCRT64/MINGW32/CLANG64). It also builds Android (NDK, all four ABIs; the x86_64
tests run statically linked), the ESP-IDF component (`ci/esp-idf`) and an Arduino sketch
(`ci/arduino`) for ESP32 and AVR. Use `ci/run-local.sh` to run the Linux-hostable
part locally (Docker for MinGW+Wine and ESP-IDF, `arduino-cli` for Arduino).

**Any other build system**: compile the seven `src/*.c` files with `-Isrc`. For speed, add
`-O2 -fno-math-errno -fno-trapping-math` (see the benchmarks below for why).

**ESP-IDF**: put the SDK folder in `components/`. The top-level `CMakeLists.txt` detects
`ESP_PLATFORM` and registers itself as a component, and lwIP sockets are used through the POSIX
backend.

**Arduino**: `library.properties` is included. On boards without BSD sockets the network layer turns
itself off (`VBAN_ENABLE_NET=0`). You then feed datagrams from `WiFiUDP` / `EthernetUDP` into
`vban_rx_process()` and send through a `vban_send_fn`.

**Bare metal**: compile with `-DVBAN_ENABLE_NET=0 -DVBAN_CUSTOM_CLOCK`, and provide
`vban_time_ns()`, `vban_sleep_until_ns()` and `vban_thread_realtime()` (the pacer uses only the
first two).

Compile-time limits are in `src/vban/vban_config.h`: `VBAN_RX_MAX_STREAMS`, `VBAN_TX_MAX_DEST`,
`VBAN_NET_BATCH`, `VBAN_PING_REPLY_RATE`, `VBAN_ENABLE_F64`.

## Usage

### Receive a stream into the audio callback
```c
#include "vban.h"

static float ring[4096 * 2];            /* 85 ms @48k stereo max; RAM = 32 KB (or 16 KB as s16) */
static vban_jb_t jb;
static vban_rx_t rx;

void setup(void) {
    vban_jb_config_t jc = {0};
    jc.channels = 2;
    jc.out_rate = 48000;                /* device rate; the stream may differ (resampled) */
    jc.adaptive = 1;                    /* settle at the lowest latency that does not underrun */
    jc.drift    = VBAN_JB_DRIFT_RESAMPLE;
    jc.target_frames = 256;             /* starting point, ~5 ms */
    vban_jb_init(&jb, &jc, ring, 4096);

    vban_rx_init(&rx, NULL);
    vban_rx_subscribe_audio(&rx, "Stream1", VBAN_IP(192,168,1,20), &jb, NULL, NULL);
}

/* network thread: call vban_rx_process() for every datagram */
void net_loop(vban_socket_t *s) {
    static uint8_t bufs[VBAN_NET_BATCH][VBAN_PACKET_MAX_SIZE];
    vban_msg_t m[VBAN_NET_BATCH];
    for (int i = 0; i < VBAN_NET_BATCH; ++i) { m[i].buf = bufs[i]; m[i].cap = sizeof bufs[i]; }
    for (;;) {
        int n = vban_udp_recv_batch(s, m, VBAN_NET_BATCH, 100);     /* 1 syscall on Linux */
        for (int i = 0; i < n; ++i) vban_rx_process(&rx, m[i].buf, m[i].len, &m[i].addr, vban_time_ns());
    }
}

/* audio thread (real-time safe: no locks, no allocation, never blocks) */
void audio_callback(float *out, int frames) { vban_jb_pull_f32(&jb, out, frames); }
```

### Transmit
```c
vban_socket_t s;  vban_udp_open(&s, NULL, 0, VBAN_SOCK_QOS | VBAN_SOCK_BROADCAST);
vban_tx_t tx;
vban_tx_config_t c = { .name = "Stream1", .sample_rate = 48000, .channels = 2,
                       .datatype = VBAN_DATATYPE_S16, .frames_per_packet = 64,
                       .send = vban_udp_sink, .send_ctx = &s };
vban_tx_init(&tx, &c);
vban_addr_t to;  vban_addr_parse("192.168.1.10", 6980, &to);  vban_tx_add_dest(&tx, &to);

void audio_in_callback(const float *in, int frames) { vban_tx_write_f32(&tx, in, frames); }
```
Samples are converted straight into the outgoing packet buffer, and a packet leaves the moment it is
full. `vban_tx_write_s16`, `_raw` (already in wire format) and `_f32_planar` are also available.

### Mixer
```c
static uint32_t mem[256];
vban_mixer_t mx;
vban_mixer_init(&mx, mem, sizeof mem, 4, 2);      /* 4 input channels -> 2 outputs */
vban_mixer_set_route(&mx, 2, 0, 0.5f);            /* input 2 -> output 0 at -6 dB */
vban_mixer_set_input_mute(&mx, 3, 1);
vban_mixer_set_limiter(&mx, VBAN_LIMIT_SOFT);
vban_mixer_process(&mx, in_ptrs, out_ptrs, frames);  /* gains ramp over the block: no clicks */
```

### Voicemeeter remote, MIDI, discovery
```c
vban_out_t cmd;  vban_out_init(&cmd, "Command1", &vm_addr, vban_udp_sink, &s);
vban_send_text(&cmd, "Strip[0].Mute=1;Bus[0].Gain=-6;", -1);

vban_out_t midi; vban_out_init(&midi, "MIDI1", &dst, vban_udp_sink, &s);
vban_send_midi(&midi, sysex, sysex_len);          /* split into multipart packets if > 1436 B */

vban_ping0_t me = {0};  me.bit_type = VBAN_PING_TYPE_RECEPTOR;  strcpy(me.device_name, "My device");
vban_rx_config_t rc = { .identity = &me, .send = vban_udp_sink, .send_ctx = &s };
vban_rx_init(&rx, &rc);                            /* now answers PING0 (max 20 replies/s) */
```

### Your own transport (Arduino `WiFiUDP`, RTOS stacks, pcap replay...)
`vban_rx_process(rx, buf, len, &from, now)` takes any datagram. Everything that sends takes a
`vban_send_fn(ctx, pkt, len, to)` callback. The socket layer is optional.

## Example tools (`examples/`)
| Tool | What it does |
|---|---|
| `vban_send` | Test tone or stdin s16 → VBAN (`arecord … \| vban_send -i host --stdin`) |
| `vban_recv` | VBAN → stdout PCM through the jitter buffer (`vban_recv -n Stream1 \| aplay -f S16_LE -r 48000 -c 2`) |
| `vban_ping` | PING0 discovery: lists devices, apps, type, rates |
| `vban_text` | Sends Voicemeeter script text and prints REQUESTREPLY answers |
| `vban_mixer_demo` | Mixes two streams into a new one and announces itself as a virtual mixer |
| `vban_monitor` | Live table of every stream heard on a port: protocol, format, pkt/s, loss |

## Latency guide
End-to-end latency = packet fill time + network + jitter-buffer target + device buffer.

| Knob | Effect |
|---|---|
| `frames_per_packet` (tx) | The sender waits this long before sending: 256 @48k = 5.3 ms, 64 = 1.3 ms, 32 = 0.67 ms. Match your audio period. |
| `target_frames` / `min_frames` (jb) | Receive buffering. With `adaptive = 1` the target grows on underrun and shrinks after 4 s without one, but never below *largest pull + packet size* (the smallest target that can work without glitches). |
| `drift` | `RESAMPLE` (default) follows the sender clock with a ±0.5 % linear-interpolation resampler, inaudible and with no buffer creep. `SLIP` drops or repeats single frames. `NONE` is for forwarders. |
| `VBAN_SOCK_QOS` | DSCP EF (46) + `SO_PRIORITY 6`: voice-class queuing on Wi-Fi WMM and managed switches. |
| `vban_thread_realtime()` | SCHED_FIFO / MMCSS "Pro Audio" / QoS user-interactive for the network or audio thread. |
| `vban_udp_recv_batch` / `_us` | `recvmmsg`: up to 16 packets per syscall on Linux. Wait for sub-ms audio deadlines with the `_us` variants (`ppoll`/`select`); passing a 0 ms timeout instead spins the CPU. |
| `vban_udp_set_buffers` | A larger `SO_RCVBUF` absorbs bursts when the receive thread is preempted. |

Loss handling (playout-deadline buffer, ADR-001):
- **Placement by frame counter.** Each packet is written at the slot its frame counter maps to, not at
  the end of a queue. So a reordered packet, or a redundant copy, still fills its hole as long as it
  arrives before that slot is played. Duplicates are recognised and dropped.
- **Concealment at playout time.** When a slot is still empty as it is played, the receiver plays a
  short decay from the last sample, then fades in when audio resumes.
  - *A hole with newer audio already buffered* is a loss. The playout clock keeps running, so latency
    does not change.
  - *Nothing newer buffered* is starvation, meaning the network is late. Playout holds and plays
    concealment, so delayed packets are still on time when they arrive. There is no
    "silence + re-prime".
- **Sender restarts and over-full buffers.** A sender restart (a large counter jump, confirmed by a
  second packet) re-anchors the buffer. After a network stall, an over-full buffer jumps forward rather
  than keeping the extra latency.

### Loss protection: redundant copies (`vban_tx_set_redundancy`)
The sender can send every packet a second time, unchanged, *n* packets later. A loss burst shorter than
*n* packets is then repaired by the copy, before playout, with no added latency. It costs 2× bandwidth
and `n × 1464` bytes of sender memory. Measured with `vban_netsim`, 64-frame packets, n = 4 (≈5 ms):

| Network | Audible loss, no copies | With copies | Latency change |
|---|---|---|---|
| Wi-Fi, good | 292 ms/min | **27 ms/min (−91 %)** | none |
| Wi-Fi, congested | 1799 ms/min | **370 ms/min (−79 %)** | none |
| WAN / VPN | 490 ms/min | **45 ms/min (−91 %)** | none |
| Wired LAN | 0 | 0 | none |

The delay should be about 5 ms. Longer delays repair less, because copies then arrive after their
playout time (n = 8 repaired 79 % instead of 91 % on Wi-Fi).

XOR parity (1 packet per 4, 1.25× bandwidth) was also simulated. It repaired only 20 % on Wi-Fi,
because burst losses take out two packets per group, and it would need a non-standard packet type, so
it was not adopted.

Copies are byte-identical packets, so any receiver that drops duplicate counters sees a normal stream.
Voicemeeter's handling of duplicates has not been verified, so enable copies only for SDK-based
receivers. Try it with `vban_send -R 4`.

### What latency to expect (measured with `vban_netsim`)
`vban_netsim` runs the real jitter buffer against simulated networks in virtual time. The networks
model Gaussian jitter, correlated FIFO delay spikes, Gilbert–Elliott burst loss and clock drift. Each
run is 120 s at 48 kHz stereo. Raw output is in `bench/NETSIM_RESULTS.txt`.

| Network | Packet / period | Jitter buffer | Mouth-to-ear | Starvations / 2 min | Physical floor\* |
|---|---|---|---|---|---|
| Wired LAN | 32 / 64 | 3.4 ms | **4.5 ms** | 0 | — |
| Wired LAN | 64 / 128 | 6.7 ms | 8.9 ms | 0 | — |
| Wi-Fi, good | 64 / 128 | 15.8 ms | 20.0 ms | 12 | 12.7 ms + 4 ms |
| Wi-Fi, congested | 64 / 128 | 40.5 ms | 55.2 ms | 37 | 43.7 ms + 4 ms |
| WAN / VPN | 64 / 128 | 34.0 ms | 65.0 ms | 29 | 33.8 ms + 4 ms |

\*Physical floor: the 99.9th-percentile delay variation of the network, plus the packet and period
granularity. The adaptive buffer settles at this floor. No buffering algorithm can go lower without
late packets. On Wi-Fi and WAN, **jitter sets the latency, not the SDK**: with jitter switched off in
the simulation, the buffer shrinks 2–4×, while switching loss off changes nothing. To lower latency,
use wired links, keep `VBAN_SOCK_QOS` on (Wi-Fi WMM voice queue), and use small packets and periods.

## Memory
The SDK never calls `malloc`. Sizes are from x86-64 and are similar on 32-bit targets.

| Object | Size |
|---|---|
| `vban_jb_t` | ~1.2 KB (256 slot tags, `VBAN_JB_TAGS`) + ring (`frames × channels × 4`, or `× 2` with `VBAN_JB_S16`) |
| `vban_mixer_t` | 80 B + `vban_mixer_mem_size()` (8×8 = 624 B) |
| `vban_tx_t` | 1552 B (contains its own 1464 B packet) |
| `vban_rx_t` | 544 B with 8 subscriptions |
| `vban_out_t` / `vban_reasm_t` | 48 B / 40 B (+ your buffer) |
| Code (`-Os`, no net, no f64) | ~23 KB x86-64, ~25 KB aarch64; 0 B `.data`/`.bss` |

Stack: the senders build packets in a ≤1464 B stack buffer. PCM chunking uses ≤2 KB.

## Benchmarks
`vban_bench` on x86-64, GCC 15, `-O2`. Full output is in `bench/RESULTS_x86_64.txt`.

| Path | Cost |
|---|---|
| Parse + validate a 256×2 s16 packet | 13 ns |
| `vban_rx_process` (8 subscriptions, match last) | 74 ns |
| tx: 256 stereo float frames → s16 packet | 0.6–0.8 µs |
| Jitter buffer push + pull, 256 frames | 0.24 µs (no drift ctl) / 1.9 µs (resampling) |
| 8×8 mixer, 128 frames, full matrix | 3.0 µs (0.36 ns per multiply-add) |
| **tx_write → UDP loopback → recv → rx_process → jb_push** | **p50 10 µs, p99 15 µs, p99.9 19 µs** |

A stereo 48 kHz stream costs well under 0.1 % of one desktop core. The SDK adds microseconds; the
latency you hear is set by the packet size and buffer targets above.

### Optimization log (measure → change one thing → re-measure)
1. Baseline profile: `f32 → s16` took 2.12 ns/sample against 0.26 for the reverse, and was about 99 %
   of the transmit cost. The disassembly showed scalar `cvtss2si`, so the loop was not vectorized.
2. First try: a sign-ternary rounding. It **regressed** to 4.0 ns (branch mispredicts on audio) and
   was rejected.
3. GCC's vectorizer report showed the real blocker: the default `-ftrapping-math` stops
   if-conversion of the float clamp. With `-fno-trapping-math` and a branchless `copysignf` rounding,
   the loop vectorizes: **2.12 → 1.14 ns/sample, tx packet 1.10 → 0.6–0.8 µs**.
4. The same change **regressed** paths that cannot vectorize (3-byte s24 stores, in-place gain).
   Those keep `lrintf`. s24 was then split into two passes (vector rounding, then byte packing):
   **2.71 → 2.29 ns**. A redundant integer clamp was removed: gain **3.37 → 2.69 ns**.
5. The resampler (7.5 ns/frame) was measured and left alone: it is about 0.04 % of one core at
   48 kHz stereo, so there was nothing worth gaining.

6. **Footprint, measured on the running tools** (48 kHz stereo, 64-frame packets, `/proc` CPU ticks
   over 10 s). `vban_recv` used 12.7 % of a core. A loop counter showed 64k empty polls per second:
   the millisecond timeout API forced a 0 ms poll for sub-ms audio deadlines. Adding microsecond waits
   (`vban_udp_wait_us`, `vban_udp_recv_batch_us` via `ppoll`/`select`) brought it to **4.1 %**, almost
   all of it kernel wakeups. `vban_send` (~7.7 %) was compared with a no-SDK C sender that only does
   `clock_nanosleep` + `sendto` per packet: that also took ~7.1 %. **The sender cost is the OS's
   per-packet floor.** The only way to lower it is fewer, larger packets, which trades directly
   against latency. RSS is about 2 MB per tool, almost all of it libc.
7. **Latency, measured with `vban_netsim`.** The adaptive target could shrink below
   *pull + packet size*, which caused 6 underruns per 2 min on a clean wired LAN with 30 ppm drift. A
   floor at that size made it **0**, at the cost of +2.2 ms, which is the true minimum. Comparing
   against the network's delay quantiles showed the buffer already sits at the jitter floor, so no
   buffer redesign was made for latency.

8. **Security sanitizing on the decode path (SEC-005).** Rejecting NaN/Inf with a float `v == v` test
   cost 0.21 → 0.8 ns/sample on f32 decode. GCC's vectorizer report gave two reasons: with no
   `restrict`, `-O2` will not add the runtime alias check, and once the helper is inlined the
   `restrict` is lost. The fix was an integer-only clamp on the sample bits, in a `noinline` helper
   with `restrict` pointers. Final cost: **f32 decode 0.50 ns/sample** (+0.29 ns, about 0.003 % of a
   core for a stereo stream) and **f32 → s16 1.71 ns** (inside the noisy 1.2–2.5 ns baseline range).

9. **Playout-deadline jitter buffer + redundancy (ADR-001).** First, a glitch metric (audible
   non-stream audio, ms/min) was added to `vban_netsim`. It showed that ~90 % of glitch time on Wi-Fi
   and WAN is the lost audio itself, and only ~10 % is underrun silence.
   - **The buffer rework** (slot placement + playout-time concealment) took the underrun share down
     (Wi-Fi good 299 → 292 ms/min, congested 1861 → 1800, 20–30 % fewer starvations) at equal or lower
     latency.
   - **Its main purpose** is to let late copies fill their holes. Time-diversity copies then cut
     audible loss by 79–91 %, with no latency cost.
   - **Two bugs found by measurement:**
     - The SEC-004 jump confirmation locked up on interleaved copies, where every packet was rejected.
       It now confirms against the last 4 suspects.
     - Copies of already-played packets counted as late and inflated the target (+2.1 ms on LAN). The
       duplicate check now runs first.
   - **Fast-path cost** is unchanged within noise (1.03 / 7.3 ns per frame without / with resampling).

10. **Kernel ring parity (`vban_parity`).** The kernel module's receive ring (`shared/vban_kring.h`, shared with the kernel module in `sotto-linux`,
    integer-only) was made buildable in userspace and run through `vban_netsim` next to the SDK
    jitter buffer, on identical networks and buffering. It found three kernel problems the VM
    tests could not: a stalled-hole lockup (14 re-primes, +66 % glitch on congested Wi-Fi), a slip
    storm (36 000 drift slips in 120 s), and slips that could not be both quiet and fast. The fix
    for the last two was a Q16 linear-interpolation resampler with the SDK's control law. Now the
    kernel is within 1 % of the SDK's glitch time on LAN/Wi-Fi, 6–37 % lower on WAN (it buffers
    1.3 ms more there), with 0 re-primes and no slips. `ctest` enforces this.

## Tests
`ctest` runs `vban_tests`, the C++ wrapper test and `vban_parity` (kernel ring vs SDK jitter
buffer, see optimization log 10). `vban_tests` has 438 checks and runs clean under ASan + UBSan (`-DVBAN_SANITIZE=ON`). The lock-free jitter buffer was also stress-tested under ThreadSanitizer, with loss, reordering, duplicates, restarts and rate changes from a producer thread against a pulling consumer thread: 0 races reported. The unit tests cover:
- spec header examples byte-for-byte (`0x52/0x10 "Command1"`, `0x2E "MIDI1"`, `0x60 00 20 0F`,
  `0x89 ll mm 0z`)
- every parse error path
- PING0 field offsets
- all PCM types with unaligned buffers, saturation and rounding
- frame-counter wrap and restart
- the jitter buffer: bit-exact passthrough, loss concealment, late drop, underrun/re-prime,
  adaptivity, channel remap, s16 ring, and 30 s of +0.1 % clock drift (ratio tracked to 1.00095 with
  no underruns, skips or discontinuities)
- mixer ramps, mutes and limiters
- tx→rx round trip
- the PING0 responder and its rate limit
- TEXT, SysEx multipart and FRAME reassembly, including loss
- RT-Packet server busy and expiry
- real UDP loopback with `recvmmsg`
- 300k fuzzed datagrams

`vban_cpp_test` checks the C++ wrapper.

**Interop:** the SDK was tested against an independent C implementation, `OpenWRT-VBAN-Hub`'s
`vbanhub`. An SDK transmitter (s24) was routed through the hub with a name rewrite and 50 % gain
back to an SDK receiver:
- 200/200 packets arrived, frame counters were preserved, and samples were within 2 LSB.
- The hub's PING0 reply decoded correctly: name, app, manufacturer, type `0xF`, rates 6–705.6 kHz.

Verified here, on a Linux host. Every build below used `-DVBAN_WERROR=ON` (or the platform's own
`--warnings all` / `-Werror=all`) and produced no warnings from SDK sources:
- **Linux x86-64**, GCC 15 and Clang 21: full test suite and C++ wrapper test, also under ASan/UBSan.
- **Linux aarch64**: cross-compiled; the test suite, including the UDP loopback, runs under qemu-user.
- **Android, NDK r30, API 21**: the library, tests, examples and benchmarks build with `-Werror` for
  arm64-v8a, armeabi-v7a, x86_64 and x86. The full test suite was also linked statically against
  bionic and run, including the UDP loopback: x86_64 natively and arm64-v8a under qemu-user, 426/426
  passing on both. The 32-bit ABIs (armeabi-v7a, x86) compile but could not be run here: 32-bit bionic
  aborts in processes with a PID above 65535, which this host has, and the host allows no PID
  namespace. Real 32-bit devices keep PIDs below 32768.
- **Windows x86-64 and i686, MinGW-w64 GCC**: cross-compiled; `vban_tests` (including Winsock
  UDP loopback) and `vban_cpp_test` run under Wine.
- **ESP-IDF v5.4.1**: the SDK builds as a component and links into an app (`ci/esp-idf`) for
  esp32 and esp32s3 (Xtensa), esp32c3 and esp32c6 (RISC-V).
- **Arduino AVR** (Mega 2560) via `arduino-cli` and `library.properties`: compiles and links with
  `VBAN_ENABLE_NET=0` and the `micros()` clock. The CI sketch, which touches every module, takes
  5.8 KB of RAM, so it does not fit 2–2.5 KB boards (Uno, Leonardo).
- **Arduino-ESP32** core 3.3.12 (esp32, esp32c3): compiles and links with the lwIP socket layer on.

Not run here: MSVC (x64, x86 and ARM64, which uses the `_InterlockedOr` atomics), clang-cl and macOS
(mach clock, QoS class, no `recvmmsg`). `.github/workflows/ci.yml` builds and
tests all of the platforms above plus these on GitHub-hosted runners, but it has not run yet: this
folder is not a git repository. `ci/run-local.sh` reproduces the jobs a Linux host can run.

Porting bugs that these builds found and fixed: ESP-IDF has no `nanosleep` (link error, now
`usleep`); AVR has no lock-free 32-bit atomics (link error, and 32-bit index access could be torn
by an ISR; now masked with `ATOMIC_BLOCK`); `find_library(m)` missed libm in cross toolchains, so
`-lm` was dropped and links failed (now a link probe); and MinGW `-Wpedantic` rejected the
`GetProcAddress` casts.

### Linux kernel sound card (`sotto-linux` repository)
`snd-vban` is an ALSA virtual sound card. Playback is sent as a VBAN stream and capture receives
one, so any Linux application (PipeWire, JACK, `aplay`/`arecord`) can use VBAN without a daemon.

It is built from the SDK's protocol code, integer-only. It was tested end to end in a QEMU VM on
the host's own kernel (7.0): a clean 440 Hz tone in both directions, redundant copies de-duplicated,
30 open/close cycles, clean unload, no kernel warnings. See the `sotto-linux` repository.

### Android notes
- **Permissions:** `android.permission.INTERNET`. To receive broadcast or multicast VBAN (and PING0
  discovery) over Wi-Fi, also hold a `WifiManager.MulticastLock` while listening; without it many
  devices filter those packets.
- **Wi-Fi latency:** hold a `WifiManager.WifiLock` in `WIFI_MODE_FULL_LOW_LATENCY` (API 29+, or
  `FULL_HIGH_PERF` before that). Wi-Fi power save otherwise batches packets into 100–300 ms bursts,
  which the adaptive buffer then has to absorb.
- **Audio thread:** call `vban_jb_pull_*` from an AAudio / Oboe data callback. For the network thread,
  `vban_thread_realtime()` tries `SCHED_FIFO` first, which apps are refused, then nice −19
  (`THREAD_PRIORITY_URGENT_AUDIO`). Whether a given device grants −19 to apps was not verified here.
- **Kotlin/Java:** the `sotto-android` repository holds a Kotlin library (`vban.sdk`: `VbanSender`,
  `VbanReceiver`, `VbanDiscovery`, `VbanCommand`, `VbanNetworkLocks`) over a JNI layer, and builds a
  128 KB AAR.

## Security
Every datagram is treated as hostile: VBAN has no authentication, and any host that can reach the port
can send packets with a forged source address. An independent audit, confirmed with ASan/UBSan
proof-of-concept programs, found no memory corruption reachable from the network. It did find
9 abuse issues, all fixed and covered by tests:

| ID | Issue | Fix |
|---|---|---|
| SEC-001/002 (HIGH) | RT-Packet registrations with a forged source turned the server into a reflector (one 29 B request drew up to 255 s of stream, about 250,000×) and could fill every slot | Registrations from non-unicast or public sources are ignored (`allow_public` opts in). Timeout capped at `VBAN_RTP_MAX_TIMEOUT_S` (30 s). At most `VBAN_RTP_MAX_PER_IP` slots per IP. |
| SEC-003 | PING0 auto-reply amplified 25× to forged, broadcast or multicast sources; one global rate bucket could be drained | No replies to broadcast, multicast or port 0. Public sources get the identity body only if they sent a full request (1:1). Per-source limit plus a separate public bucket, so LAN discovery is never starved. Rate limiting can no longer be turned off. |
| SEC-004 | One forged frame counter caused 31 dropped packets plus concealment | Jumps over 4 packets must be confirmed by a second packet near them (also when copies or reordering interleave). If the old sequence keeps going with two packets in a row after a restart-size jump, the tracker resyncs back to it. |
| SEC-005 | NaN, Inf or 1e30 float samples reached the output; NaN → int was undefined behaviour | Float samples arriving from the network are clamped: NaN → 0, ±Inf or huge → ±1. `clampf` is NaN-safe. |
| SEC-006 | Any packet could change the stream's sample rate | A rate change needs `VBAN_JB_RATE_CONFIRM` (3) agreeing packets in a row |
| SEC-007 | Truncated datagrams were parsed as valid shorter packets | `MSG_TRUNC` / `WSAEMSGSIZE` are detected and the datagram is dropped |
| SEC-008 | Example tools printed network strings straight to the terminal (ANSI injection) | C0/C1 controls and malformed UTF-8 are replaced before printing |
| SEC-009 | Caller misuse: empty heard table, mixer size overflow on 32-bit, unvalidated packets reaching callbacks | Argument checks; `vban_mixer_mem_size` returns 0 on overflow; `vban_packet_t.valid` |

**Limits of what spec-compatible code can do.** Anyone who can send to the port can still inject audio
into a stream, cancel an RT subscription (timeout 0), or keep disturbing a stream by sending forged
packets non-stop. These need authentication: use secure VBAN (next section) between SDK devices, or
keep plain VBAN on trusted networks or inside a VPN such as WireGuard.

## The Sotto project
This repository is the SDK. The rest of Sotto lives in sibling repositories that use it as a git
submodule:

| Repository | What it is |
|---|---|
| `sotto` | The umbrella project: every repository below as a submodule, plus the website |
| `sotto-sdk` | This C99 SDK: protocol, jitter buffer, mixer, secure VBAN, tests |
| `sotto-app` | `sotto`: VBAN to and from any sound device on Windows, macOS and Linux |
| `sotto-linux` | ALSA kernel sound card `snd-vban`, and "Sotto in" / "Sotto out" on PipeWire |
| `sotto-windows` | "Sotto out" virtual sound card for Windows (a fork of the Scream driver) |
| `sotto-macos` | "Sotto Out" / "Sotto In" virtual devices for macOS (CoreAudio plug-in) |
| `sotto-android` | Kotlin library for Android |
| `sotto-hub` | OpenWrt hub: routing, mixer, secure VBAN CA, LuCI, eBPF fast path |

## Secure VBAN (`vban_secure.h`, ADR-003)
An optional layer (`-DVBAN_ENABLE_SECURE=ON`, the default) that authenticates and encrypts VBAN
between SDK devices and the hub. It does not change plain VBAN, and Voicemeeter keeps working
unencrypted; the OpenWrt hub bridges between the two. The hub has a CA and a Security tab in LuCI.

- **Crypto.** Monocypher 4.0.3 (BSD-2 / CC-0), vendored unmodified in `third_party/monocypher`. Its
  SHA-512 was checked against monocypher.org and the GitHub release. No crypto is written here; this
  layer only puts the primitives together as specified.
- **Certificates.** A network CA (EdDSA key) signs device certificates. A certificate is 168 bytes:
  name, role, validity, serial, key, issuer and signature, carried as base64 text. Each device
  generates its own X25519 key and sends the CA only a request, so private keys never leave the
  device. X.509 is not used: its parser is too big and too risky for an MCU.
- **Handshake.** `Noise_XX_25519_ChaChaPoly_BLAKE2b`: three messages, mutual authentication with the
  certificates, forward secrecy. Each side checks that the peer's certificate is from the CA, covers
  the key the peer proved it holds, is within its dates (when the clock is known), and passes an
  optional `authorize` callback for revocation and roles.
- **Packets.** Each VBAN packet is encrypted whole, header included, with ChaCha20-Poly1305 and sent
  inside a VBAN `USER` (0xE0) packet, which plain receivers ignore. The 28-byte outer header (type,
  session indices, 64-bit counter) is authenticated. Overhead is 44 bytes, so the inner packet is
  at most 1420 bytes (audio payload ≤ 1392). A 64-packet window rejects replays and accepts
  reordering.
- **Lifetime.** The initiator rekeys every 120 s; keys are refused after 180 s. The old receive key
  stays usable for 10 s, so packets in flight are not lost. An unanswered handshake is retried every
  second.

```bash
vban_cert ca "Studio CA" 3650 ca.key ca.crt          # once; keep ca.key safe
vban_cert key mic.key                                # on the device
vban_cert req mic.key "PC mic" tx mic.req            # send mic.req to the CA
vban_cert sign ca.key ca.crt mic.req 365 mic.crt     # at the CA; give mic.crt back
vban_recv -n Stream1 --ca ca.crt --cert rx.crt --key rx.key | aplay -f S16_LE -r 48000 -c 2
vban_send -i 192.168.1.20 -n Stream1 --ca ca.crt --cert mic.crt --key mic.key
vban_recv -n Out --hub 192.168.2.1 --ca ca.crt --cert rx.crt --key rx.key   # receive from a secure hub
```
Devices always start the session (initiator). A receiver behind the hub uses `--hub`, so the hub can
send its routed streams into that session. This also makes secure VBAN work across the internet
and through NAT. An idle link sends a 44-byte keepalive every 15 s (`VBAN_SEC_KEEPALIVE_MS`), so
NAT mappings stay open for devices that only receive. The responder follows a device to a new
address on its next authenticated packet. The hub's README describes the setup and the
simulated-internet test.

In code, a transmitter wraps its send callback with `vban_sec_link_seal()` and feeds replies to
`vban_sec_link_handle()` / `vban_sec_link_tick()`. A receiver keeps a `vban_sec_server_t` (a table of
peers you allocate) and passes each opened packet to `vban_rx_process()`. See `examples/vban_send.c`
and `examples/vban_recv.c`.

**Cost** (x86-64, `-O2`):

| Item | Value |
|---|---|
| Seal + open, 64-frame stereo s16 packet (284 B) | 3.7 µs together |
| Seal + open, 1052 B packet | 9.6 µs together |
| Seal + open, 1420 B packet | 12.9 µs together |
| Full handshake, both sides | 2.2 ms |
| One session | 680 B |
| Initiator link (two sessions) | 1.4 KB |
| Code, aarch64 `-Os` | Monocypher 36 KB, secure layer 15 KB |

At 750 packets/s, encryption costs about 0.3 % of a core. Only what you call gets linked from the
static library, but Monocypher is one object: link with `-ffunction-sections -Wl,--gc-sections` to
drop its unused parts.

**Tests.** `vban_secure_test` (in `ctest`, 191 checks) covers:
- the `Noise_XX_25519_ChaChaPoly_BLAKE2b` vector from the cacophony suite: all three handshake
  messages, the handshake hash and the transport messages, byte for byte;
- every single-bit change of a certificate, and every single-bit change of a DATA packet (all
  rejected);
- wrong CA on either side, an expired certificate, a refusal by the `authorize` callback, a stolen
  certificate without its key, and a tampered session index;
- replay, reordering, and expiry;
- retry, rekey with the old key still accepted, and peer roaming;
- the responder's table limit, the rate limit, eviction of half-open handshakes, and the cookie
  (sent only over budget, used once, rejected from another address);
- `vban_tx` → seal → open → `vban_rx` end to end.

Over real UDP, `vban_send` → `vban_recv` in secure mode delivered a tone with 0 loss at the same
latency as plain mode. A plain sender (1125 packets) and a sender with a certificate from another CA
were both refused.

**Limits:**
- **Handshake floods.** A responder's handshake budget (20/s by default) bounds its CPU cost. Once
  the budget is used up, a handshake start gets a COOKIE reply: 44 bytes, smaller than the request,
  and no state. Only an initiator that is really at its address can resend with the cookie, so
  floods from forged addresses cannot lock real peers out. A full table evicts its oldest
  unfinished handshake, never an established session. The hub adds a per-address limit on top.
- Devices without a clock (`now_unix = 0`) cannot check validity dates; revocation through
  `authorize` still works.
- Windows: the secure checks pass under Wine (win64 and win32, `BCryptGenRandom`); the CI's mingw stage
  runs them. Not yet run on ESP-IDF or Android. It does compile for Cortex-M4 and aarch64.
- On the hub (MT7981, 2× A53), decrypting and routing secure packets costs about 20 µs per packet
  more than plain forwarding: 8 secure streams used 24 % of a core.

## Limits
- IPv4 only. The VBAN ecosystem, including PING0's `DistantIP_ascii`, is IPv4.
- The proprietary VBCA/VBCV codecs and 10/12-bit PCM cannot be decoded; they are passed through.
- The resampler is linear interpolation. That is ideal for clock-drift correction (±0.5 %). For a
  large rate conversion (44.1 → 48 kHz) it works but is not hi-fi; do that in your audio stack.
