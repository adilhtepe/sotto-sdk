# Graph Report - SDK  (2026-11-12)

## Corpus Check
- 57 files · ~54,694 words
- Verdict: corpus is large enough that graph structure adds value.
- Unclassified: 12 file(s) not represented in the graph (top: .properties 3, (none) 2, .jar 1)

## Summary
- 1120 nodes · 2415 edges · 76 communities (55 shown, 21 thin omitted)
- Extraction: 76% EXTRACTED · 24% INFERRED · 0% AMBIGUOUS · INFERRED: 577 edges (avg confidence: 0.85)
- Token cost: 0 input · 0 output

## Graph Freshness
- Built from commit: `599c6244`
- Run `git rev-parse HEAD` and compare to check if the graph is stale.
- Run `graphify update .` after code changes (no API cost).

## Community Hubs (Navigation)
- vban_jitter.c
- Jitter Buffer State
- Mixer & C++ Wrapper
- vban_proto.h
- Kernel ALSA Driver
- vban_rx_subscribe_audio
- JNI Glue
- PCM Conversion
- vban_net.c
- Audio Transmitter
- main
- Kotlin JVM Tests
- string
- vban_kring.h
- PING0 Identity Struct
- Receiver State
- Docs: Android API
- Kotlin Native Bridge
- vban.h
- CI Windows Jobs
- CI Job Matrix
- Parsed Packet View
- Transmitter State
- Benchmarks & Build Targets
- Jitter Buffer Stats
- Send Hook & Data Streams
- common.h
- Local CI Runner
- CI Arduino Jobs
- Error Codes
- Sequence Tracker State
- Netsim & Redundancy Docs
- CI Workflow Root
- vban_pacer
- Kotlin Receiver
- Kotlin Sender
- RT-Packet Server
- Android Build & Tests Docs
- Jitter Buffer Config
- vban_rx_stats
- Android Network Locks
- Kernel Driver Docs
- CI Cross Matrix
- vban_udp_send
- Receiver Config
- Kotlin Text Commands
- Memory Footprint Docs
- Reassembly Buffer
- Kotlin Core Types
- CI MinGW Jobs
- Sequence Results
- Spec Interpretation Docs
- Native Handle Guard
- Realtime Thread Boost
- vban_rx_stream
- vban_heard
- CI ESP-IDF Job
- Float Safety Policy
- rx_pull
- Kernel Capture Fix
- Kernel VM Test
- Config Limits Doc
- vban_out
- kring_pull
- vban_msg
- vban_tx_stats
- Error
- vban_rtp_client

## God Nodes (most connected - your core abstractions)
1. `vban_jb` - 60 edges
2. `main()` - 35 edges
3. `vban_addr_t` - 34 edges
4. `main()` - 28 edges
5. `vban_jb_t` - 26 edges
6. `VbanNative` - 24 edges
7. `vban_ci_smoke()` - 23 edges
8. `vban_tx` - 22 edges
9. `vban_rx_process()` - 22 edges
10. `main()` - 21 edges

## Surprising Connections (you probably didn't know these)
- `main()` --calls--> `run()`  [INFERRED]
  tests/test_parity.c → bench/netsim.c
- `PING0 device names cut to printable ASCII` --semantically_similar_to--> `SEC-007..009 truncation, ANSI injection, caller misuse`  [INFERRED] [semantically similar]
  android/README.md → README.md
- `Kernel receive thread with frame-counter ring` --semantically_similar_to--> `Playout-deadline jitter buffer (ADR-001)`  [INFERRED] [semantically similar]
  kernel/README.md → README.md
- `RAM footprint (no heap)` --semantically_similar_to--> `Memory footprint (SDK never calls malloc)`  [INFERRED] [semantically similar]
  bench/RESULTS_x86_64.txt → README.md
- `run()` --calls--> `kring_fill()`  [INFERRED]
  bench/netsim.c → kernel/vban_kring.h

## Import Cycles
- None detected.

## Hyperedges (group relationships)
- **VBAN SDK core C modules** — readme_vban_proto_module, readme_vban_pcm_module, readme_vban_jitter_module, readme_vban_mixer_module, readme_vban_rx_module, readme_vban_tx_module, readme_vban_net_module [EXTRACTED 1.00]
- **Loss handling: frame-counter placement, redundant copies, playout concealment** — readme_vban_seq, readme_playout_deadline_buffer, readme_redundant_copies, bench_netsim_results_fec_dup4, kernel_readme_receive_thread [INFERRED 0.85]
- **Build targets consuming the vban library** — cmakelists_vban, android_vban_src_main_cpp_cmakelists_vban_jni, ci_esp_idf_cmakelists_vban_idf_smoke, cmakelists_vban_tests, cmakelists_vban_bench, cmakelists_vban_netsim [INFERRED 0.85]

## Communities (76 total, 21 thin omitted)

### Community 0 - "vban_jitter.c"
Cohesion: 0.07
Nodes (57): run(), kring_init(), conceal_frame(), copy_out(), frame_valid(), get_anchor(), is_pow2(), pull() (+49 more)

### Community 1 - "Jitter Buffer State"
Cohesion: 0.04
Nodes (56): vban_jb, adaptive, aseq, c_cur, c_prev, channels, conc_gain, concealed (+48 more)

### Community 2 - "Mixer & C++ Wrapper"
Cohesion: 0.07
Nodes (37): Mixer, finish_output(), mem_, mx_, soft_clip(), target_gain(), vban_db_to_gain(), vban_gain_to_db() (+29 more)

### Community 3 - "vban_proto.h"
Cohesion: 0.05
Nodes (96): on_ping_reply(), ex_safe_text(), on_any(), on_service(), on_reply(), rx_thread(), snd_vban_init(), vban_addr_format() (+88 more)

### Community 4 - "Kernel ALSA Driver"
Cohesion: 0.08
Nodes (9): open_sockets(), parse_dest(), snd_vban_exit(), stream_of(), vban_close(), vban_open(), vban_pointer(), vban_prepare() (+1 more)

### Community 5 - "vban_rx_subscribe_audio"
Cohesion: 0.31
Nodes (7): Receiver, rx_, vban_rx_subscribe(), vban_rx_subscribe_audio(), vban_rx_unsubscribe(), vban_rx_cb, vban_rx_t

### Community 6 - "JNI Glue"
Cohesion: 0.19
Nodes (29): get_str(), Java_vban_sdk_VbanNative_cmdCreate(), Java_vban_sdk_VbanNative_cmdDestroy(), Java_vban_sdk_VbanNative_cmdSendText(), Java_vban_sdk_VbanNative_discover(), Java_vban_sdk_VbanNative_rxCreate(), Java_vban_sdk_VbanNative_rxDestroy(), Java_vban_sdk_VbanNative_rxLocalPort() (+21 more)

### Community 7 - "PCM Conversion"
Cohesion: 0.19
Nodes (34): clampf(), f2i(), f2i32(), f2i_s24(), f32_bits_san(), f32_to_s24(), f32_wire_to_f32(), f32_wire_to_s16() (+26 more)

### Community 8 - "vban_net.c"
Cohesion: 0.10
Nodes (17): from_sa(), recv_one(), to_sa(), vban_udp_join_multicast(), vban_udp_local_port(), vban_udp_recv(), vban_udp_recv_batch(), vban_udp_recv_batch_us() (+9 more)

### Community 9 - "Audio Transmitter"
Cohesion: 0.17
Nodes (23): conv_f32(), conv_s16(), emit(), out_send(), vban_out_init(), vban_send_frame(), vban_send_midi(), vban_send_ping() (+15 more)

### Community 10 - "main"
Cohesion: 0.26
Nodes (30): main(), vban_ci_smoke(), arg_flag(), arg_int(), arg_str(), ex_init(), main(), main() (+22 more)

### Community 12 - "string"
Cohesion: 0.11
Nodes (11): Addr, addr(), check(), Net, Sender, tx_, vban_tx_flush(), Cap (+3 more)

### Community 13 - "vban_kring.h"
Cohesion: 0.14
Nodes (7): vban_proc_read(), vban_trigger(), get_unaligned_le16(), kring_fill(), kring_place(), kring_start(), kring_stop()

### Community 14 - "PING0 Identity Struct"
Cohesion: 0.10
Nodes (20): vban_ping0, application_name, bit_feature, bit_feature_ex, bit_type, color_rgb, device_name, distant_ip (+12 more)

### Community 15 - "Receiver State"
Cohesion: 0.19
Nodes (12): vban_heard_t, vban_rx, cfg, vban_rx_config_t, ping_refill_ns, ping_src_ip, ping_src_ns, ping_tokens (+4 more)

### Community 16 - "Docs: Android API"
Cohesion: 0.14
Nodes (16): VbanDiscovery, VbanReceiver, Latency guide, Limits (IPv4 only, no VBCA/VBCV decode, linear resampler), SERVICE PING0 discovery, Protocol coverage table, SERVICE RT-Packet register/server, SEC-001/002 RT-Packet reflector / slot exhaustion (+8 more)

### Community 18 - "vban.h"
Cohesion: 0.11
Nodes (7): null_send(), gauss(), main(), urand(), smoke_send(), app_main(), main()

### Community 19 - "CI Windows Jobs"
Cohesion: 0.16
Nodes (19): steps, steps, windows-arm64-cross, windows-arm64-native, steps, steps, steps, if (+11 more)

### Community 20 - "CI Job Matrix"
Cohesion: 0.13
Nodes (18): name, runs-on, strategy, strategy, android, linux-cross, native, name (+10 more)

### Community 21 - "Parsed Packet View"
Cohesion: 0.11
Nodes (18): vban_packet, channels, codec, data, data_len, datatype, format_bit, format_nbc (+10 more)

### Community 22 - "Transmitter State"
Cohesion: 0.11
Nodes (18): vban_tx, channels, datatype, dest, fill, fpp, frame, n_dest (+10 more)

### Community 23 - "Benchmarks & Build Targets"
Cohesion: 0.14
Nodes (15): VBAN SDK 1.0.0 x86_64 benchmark results, End-to-end tx_write to jb_push loopback latency (p50 10 us), vban_idf_smoke ESP-IDF CI project, ESP-IDF main component (main.c, arduino/vban_smoke include), ESP-IDF component mode (ESP_PLATFORM branch), vban static library target, vban_bench executable, VBAN build options (TESTS/EXAMPLES/BENCH/ENABLE_NET/SANITIZE/WERROR) (+7 more)

### Community 24 - "Jitter Buffer Stats"
Cohesion: 0.12
Nodes (16): vban_jb_stats, concealed_frames, dup, fill_frames, late, lost, overruns, packets (+8 more)

### Community 25 - "Send Hook & Data Streams"
Cohesion: 0.25
Nodes (8): vban_tx_config, channels, datatype, frames_per_packet, name, sample_rate, send, send_ctx

### Community 26 - "common.h"
Cohesion: 0.15
Nodes (3): format_name(), parse_format(), protocol_name()

### Community 27 - "Local CI Runner"
Cohesion: 0.29
Nodes (11): cmake_test(), have(), run_stage(), run-local.sh script, skip(), st_aarch64(), st_arduino(), st_asan() (+3 more)

### Community 28 - "CI Arduino Jobs"
Cohesion: 0.15
Nodes (13): name, runs-on, steps, arduino, with, cli-compile-flags, fqbn, libraries (+5 more)

### Community 29 - "Error Codes"
Cohesion: 0.15
Nodes (13): vban_err, VBAN_E_ARG, VBAN_E_FORMAT, VBAN_E_MAGIC, VBAN_E_NET, VBAN_E_NOSPACE, VBAN_E_NOTFOUND, VBAN_E_OVERSIZE (+5 more)

### Community 30 - "Sequence Tracker State"
Cohesion: 0.15
Nodes (13): vban_seq, cand, expected, has_cand, has_prev, hist, max_gap, n_hist (+5 more)

### Community 31 - "Netsim & Redundancy Docs"
Cohesion: 0.21
Nodes (8): VbanSender, Netsim fec dup:4 (redundant copies) run, Netsim fec xor:4 (XOR parity) run, Netsim results: wired LAN, Wi-Fi good/congested, WAN/VPN, vban_netsim executable, Kernel receive thread with frame-counter ring, vban_netsim network simulator, vban_tx (zero-copy packetizer, TEXT/SERIAL/MIDI/FRAME/PING senders, pacer)

### Community 32 - "CI Workflow Root"
Cohesion: 0.17
Nodes (11): concurrency, cancel-in-progress, group, jobs, name, on, pull_request, push (+3 more)

### Community 33 - "vban_pacer"
Cohesion: 0.29
Nodes (6): vban_pacer, frames, rate, start_ns, vban_tx_config_t, vban_tx_stats_t

### Community 36 - "RT-Packet Server"
Cohesion: 0.17
Nodes (13): vban_rtp_server_init(), vban_send_fn, vban_rtp_client_t, vban_rtp_server, allow_public, clients, frame, n_clients (+5 more)

### Community 37 - "Android Build & Tests Docs"
Cohesion: 0.24
Nodes (7): Composite build integration (includeBuild, vban.sdk:vban:1.0.0), Kotlin JVM unit tests (testDebugUnitTest, 9/9 pass), VBAN SDK for Android (Kotlin library vban.sdk), VbanException (VBAN_E_* codes), VbanNetworkLocks (MulticastLock + low-latency WifiLock), vban_jni shared library, Android notes (MulticastLock, WifiLock, AAudio/Oboe, nice -19)

### Community 38 - "Jitter Buffer Config"
Cohesion: 0.20
Nodes (10): vban_jb_config, adaptive, channels, drift, max_frames, max_gap, min_frames, out_rate (+2 more)

### Community 39 - "vban_rx_stats"
Cohesion: 0.29
Nodes (7): vban_rx_stats, invalid, packets, pings_answered, pings_limited, pings_refused, unmatched

### Community 41 - "Kernel Driver Docs"
Cohesion: 0.22
Nodes (8): Example tool executables (vban_send, vban_recv, vban_ping, vban_text, vban_mixer_demo, vban_monitor), SCHED_FIFO hrtimer clock thread, snd-vban module parameters (dest, tx_name, listen_port, rx_name, rate, fpp, rx_target), Bug: example tools ignored SIGTERM (fixed), snd-vban ALSA virtual sound card driver, vm-test.sh QEMU/virtme-ng tests, Example tools (examples/), OpenWRT-VBAN-Hub (vbanhub) interop

### Community 42 - "CI Cross Matrix"
Cohesion: 0.22
Nodes (9): arch, args, fqbn, name, os, platforms, qemu, triple (+1 more)

### Community 43 - "vban_udp_send"
Cohesion: 0.22
Nodes (4): vban_udp_send(), vban_udp_sink(), Socket, s_

### Community 44 - "Receiver Config"
Cohesion: 0.22
Nodes (9): vban_rx_config, identity, on_service, on_unmatched, parse_flags, ping_full_to_public, send, send_ctx (+1 more)

### Community 46 - "Memory Footprint Docs"
Cohesion: 0.25
Nodes (7): RAM footprint (no heap), vban_cpp_test executable, Memory footprint (SDK never calls malloc), vban.hpp header-only C++11 RAII wrapper, vban_mixer (N x M matrix mixer, ramps, limiter), vban_net (UDP POSIX/Winsock, recvmmsg/sendmmsg, clock, RT priority), VBAN SDK

### Community 47 - "Reassembly Buffer"
Cohesion: 0.25
Nodes (8): vban_reasm, active, buf, cap, dropped, frame, len, next

### Community 48 - "Kotlin Core Types"
Cohesion: 0.29
Nodes (3): VbanDevice, VbanException, VbanFormat

### Community 49 - "CI MinGW Jobs"
Cohesion: 0.29
Nodes (7): run, mingw, defaults, name, runs-on, strategy, shell

### Community 50 - "Sequence Results"
Cohesion: 0.29
Nodes (7): vban_seq_result, VBAN_SEQ_FIRST, VBAN_SEQ_GAP, VBAN_SEQ_LATE, VBAN_SEQ_OK, VBAN_SEQ_RESET, VBAN_SEQ_SUSPECT

### Community 51 - "Spec Interpretation Docs"
Cohesion: 0.33
Nodes (5): VbanCommand, VBAN_SOURCES (seven src/*.c files), shim/ mapping userspace headers to kernel headers (integer-only), vban_proto (wire format, parse/build, PING0, sequencing), Voicemeeter

### Community 54 - "vban_rx_stream"
Cohesion: 0.20
Nodes (10): vban_rx_stream, active, cb, ip, jb, name, nocase, packets (+2 more)

### Community 55 - "vban_heard"
Cohesion: 0.22
Nodes (9): vban_heard, format_bit, format_nbc, format_SR, from, last_ns, name, packets (+1 more)

### Community 56 - "CI ESP-IDF Job"
Cohesion: 0.40
Nodes (5): container, name, runs-on, strategy, esp-idf

### Community 61 - "rx_pull"
Cohesion: 0.33
Nodes (4): clock_thread(), do_capture(), do_playback(), rx_pull()

### Community 68 - "vban_out"
Cohesion: 0.33
Nodes (6): vban_out, dest, frame, name, send, send_ctx

### Community 69 - "kring_pull"
Cohesion: 0.60
Nodes (3): kring_conceal_frame(), kring_frame_valid(), kring_pull()

### Community 70 - "vban_msg"
Cohesion: 0.40
Nodes (5): vban_msg, addr, buf, cap, len

### Community 71 - "vban_tx_stats"
Cohesion: 0.40
Nodes (5): vban_tx_stats, bytes, errors, packets, redundant

### Community 73 - "vban_rtp_client"
Cohesion: 0.50
Nodes (4): vban_rtp_client, addr, expire_ns, ids

## Knowledge Gaps
- **302 isolated node(s):** `epoch`, `frame`, `nbs`, `pos`, `valid` (+297 more)
  These have ≤1 connection - possible missing edges or undocumented components. (Counts symbols only; 456 node(s) total have ≤1 connection when file, concept and rationale nodes are included.)
- **21 thin communities (<3 nodes) omitted from report** — run `graphify query` to explore isolated nodes.

## Suggested Questions
_Questions this graph is uniquely positioned to answer:_

- **Why does `vban_jb` connect `Jitter Buffer State` to `vban_jitter.c`, `vban_proto.h`?**
  _High betweenness centrality (0.093) - this node is a cross-community bridge._
- **Are the 34 inferred relationships involving `main()` (e.g. with `vban_jb_init()` and `vban_jb_pull_f32()`) actually correct?**
  _`main()` has 34 INFERRED edges - model-reasoned connections that need verification._
- **What connects `epoch`, `frame`, `nbs` to the rest of the system?**
  _302 weakly-connected nodes found - possible documentation gaps or missing edges._
- **Should `vban_jitter.c` be split into smaller, more focused modules?**
  _Cohesion score 0.06690140845070422 - nodes in this community are weakly interconnected._
- **Why does `vban_addr_t` connect `vban_proto.h` to `vban_out`, `JNI Glue`, `vban_msg`, `vban_net.c`, `Audio Transmitter`, `main`, `vban_udp_send`, `vban_rtp_client`, `string`, `vban.h`, `Transmitter State`, `vban_heard`?**
  _High betweenness centrality (0.046) - this node is a cross-community bridge._
- **Are the 27 inferred relationships involving `main()` (e.g. with `arg_flag()` and `arg_int()`) actually correct?**
  _`main()` has 27 INFERRED edges - model-reasoned connections that need verification._
- **Should `Jitter Buffer State` be split into smaller, more focused modules?**
  _Cohesion score 0.03571428571428571 - nodes in this community are weakly interconnected._