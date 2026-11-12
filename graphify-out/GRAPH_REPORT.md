# Graph Report - SDK  (2026-11-12)

## Corpus Check
- 56 files · ~44,222 words
- Verdict: corpus is large enough that graph structure adds value.
- Unclassified: 5 file(s) not represented in the graph (top: (none) 1, .ino 1, .defaults 1)

## Summary
- 892 nodes · 1848 edges · 70 communities (50 shown, 20 thin omitted)
- Extraction: 73% EXTRACTED · 27% INFERRED · 0% AMBIGUOUS · INFERRED: 508 edges (avg confidence: 0.86)
- Token cost: 0 input · 0 output

## Community Hubs (Navigation)
- main
- test_cpp.cpp
- vban_proto.c
- vban_tx_t
- vban_mixer.c
- vban_jb
- vban_rtp_server
- vban_pcm.c
- vban_rx_t
- vban_ping0
- vban_tx
- vban_packet
- vban_proto.h
- vban_jb_config
- vban_mixer
- vban_jb_stats
- vban_err
- test_main.c
- vban_rx.h
- vban_rx_stream
- vban_heard
- vban_tx.c
- Security review — vban-sdk — 2026-11-12
- vban_seq_result
- run
- JitterBuffer
- vban_seq
- vban_rtp_client
- vban_reasm
- vban_rx_config
- jobs
- matrix
- run-local.sh
- steps
- include
- with
- vban_header
- vban_tx_config
- vban.hpp
- mingw
- Security — vban-sdk
- Handoff
- arduino
- native
- windows-arm64-native
- vban_jb_anchor
- architect/_index.md
- cto/_index.md
- cto/vban-sdk.md
- dba/_index.md
- devops/_index.md
- devops/vban-sdk.md
- engineering-manager/_index.md
- product-manager/_index.md
- qa/_index.md
- sde/_index.md
- security/_index.md
- senior-engineer/_index.md
- sre/_index.md
- staff-engineer/_index.md
- staff-engineer/vban-sdk.md
- status.md
- vban_rx_stats
- vban_msg
- Error
- vban_pacer
- vban_addr

## God Nodes (most connected - your core abstractions)
1. `vban_jb` - 60 edges
2. `main()` - 35 edges
3. `vban_addr_t` - 32 edges
4. `main()` - 28 edges
5. `vban_jb_t` - 26 edges
6. `vban_rx_process()` - 24 edges
7. `vban_ci_smoke()` - 23 edges
8. `vban_tx` - 22 edges
9. `main()` - 21 edges
10. `vban_jb_push()` - 21 edges

## Surprising Connections (you probably didn't know these)
- `Android notes` --references--> `vban_thread_realtime()`  [INFERRED]
  README.md → src/vban_net.c
- `Transmit` --references--> `vban_tx_write_s16()`  [INFERRED]
  README.md → src/vban_tx.c
- `Trust boundary` --references--> `vban_rx_process()`  [INFERRED]
  .10x/decisions/architect/vban-sdk.md → src/vban_rx.c
- `Threat model (confirmed with user)` --references--> `vban_rx_process()`  [INFERRED]
  .10x/decisions/security/vban-sdk.md → src/vban_rx.c
- `Loss protection: redundant copies (`vban_tx_set_redundancy`)` --references--> `vban_tx_set_redundancy()`  [INFERRED]
  README.md → src/vban_tx.c

## Import Cycles
- None detected.

## Communities (70 total, 20 thin omitted)

### Community 0 - "main"
Cohesion: 0.06
Nodes (65): 2026-11-12 — performance pass (measured, see README "Optimization log" 6–7), main(), vban_ci_smoke(), arg_flag(), arg_int(), arg_str(), ex_init(), ex_safe_text() (+57 more)

### Community 1 - "test_cpp.cpp"
Cohesion: 0.20
Nodes (4): Cap, pkts, cap_send(), main()

### Community 2 - "vban_proto.c"
Cohesion: 0.06
Nodes (75): 2026-11-12 — security fixes (SEC-001..009), on_any(), build_data(), lower_ascii(), parse_body(), put_header(), put_str(), rd32_avail() (+67 more)

### Community 3 - "vban_tx_t"
Cohesion: 0.18
Nodes (13): Sender, tx_, emit(), vban_tx_clear_dest(), vban_tx_flush(), vban_tx_set_name(), vban_tx_write_f32_planar(), vban_tx_write_raw() (+5 more)

### Community 4 - "vban_mixer.c"
Cohesion: 0.07
Nodes (38): Android notes, Example tools (`examples/`), Interpretations where the spec is silent or ambiguous, Limits, Memory, Mixer, Protocol coverage, Receive a stream into the audio callback (+30 more)

### Community 5 - "vban_jb"
Cohesion: 0.04
Nodes (56): vban_jb, adaptive, aseq, c_cur, c_prev, channels, conc_gain, concealed (+48 more)

### Community 6 - "vban_rtp_server"
Cohesion: 0.20
Nodes (10): vban_rtp_server, allow_public, clients, frame, n_clients, name, offered, refused (+2 more)

### Community 7 - "vban_pcm.c"
Cohesion: 0.16
Nodes (37): clampf(), f2i(), f2i32(), f2i_s24(), f32_bits_san(), f32_to_s24(), f32_wire_to_f32(), f32_wire_to_s16() (+29 more)

### Community 8 - "vban_rx_t"
Cohesion: 0.33
Nodes (4): Receiver, rx_, vban_rx_subscribe(), vban_rx_cb

### Community 9 - "vban_ping0"
Cohesion: 0.10
Nodes (20): vban_ping0, application_name, bit_feature, bit_feature_ex, bit_type, color_rgb, device_name, distant_ip (+12 more)

### Community 10 - "vban_tx"
Cohesion: 0.08
Nodes (24): vban_tx, channels, datatype, dest, fill, fpp, frame, n_dest (+16 more)

### Community 11 - "vban_packet"
Cohesion: 0.11
Nodes (18): vban_packet, channels, codec, data, data_len, datatype, format_bit, format_nbc (+10 more)

### Community 12 - "vban_proto.h"
Cohesion: 0.19
Nodes (6): vban_socket, fd, vban_err_t, vban_name, b, vban_tx_config_t

### Community 13 - "vban_jb_config"
Cohesion: 0.20
Nodes (10): vban_jb_config, adaptive, channels, drift, max_frames, max_gap, min_frames, out_rate (+2 more)

### Community 14 - "vban_mixer"
Cohesion: 0.14
Nodes (14): vban_mixer, cur, dirty, in_gain, in_mute, limiter, master, master_mute (+6 more)

### Community 15 - "vban_jb_stats"
Cohesion: 0.12
Nodes (16): vban_jb_stats, concealed_frames, dup, fill_frames, late, lost, overruns, packets (+8 more)

### Community 16 - "vban_err"
Cohesion: 0.15
Nodes (13): vban_err, VBAN_E_ARG, VBAN_E_FORMAT, VBAN_E_MAGIC, VBAN_E_NET, VBAN_E_NOSPACE, VBAN_E_NOTFOUND, VBAN_E_OVERSIZE (+5 more)

### Community 17 - "test_main.c"
Cohesion: 0.10
Nodes (46): run(), conceal_frame(), copy_out(), frame_valid(), get_anchor(), is_pow2(), pull(), put_anchor() (+38 more)

### Community 18 - "vban_rx.h"
Cohesion: 0.18
Nodes (13): vban_heard_t, vban_rtp_client_t, vban_rx, cfg, vban_rx_config_t, ping_refill_ns, ping_src_ip, ping_src_ns (+5 more)

### Community 19 - "vban_rx_stream"
Cohesion: 0.20
Nodes (10): vban_rx_stream, active, cb, ip, jb, name, nocase, packets (+2 more)

### Community 20 - "vban_heard"
Cohesion: 0.22
Nodes (9): vban_heard, format_bit, format_nbc, format_SR, from, last_ns, name, packets (+1 more)

### Community 21 - "vban_tx.c"
Cohesion: 0.49
Nodes (10): out_send(), vban_out_init(), vban_send_frame(), vban_send_midi(), vban_send_ping(), vban_send_serial(), vban_send_text(), vban_send_text_ex() (+2 more)

### Community 22 - "Security review — vban-sdk — 2026-11-12"
Cohesion: 0.40
Nodes (4): Performance impact, Residual (protocol-inherent, needs authentication), Result, Security review — vban-sdk — 2026-11-12

### Community 23 - "vban_seq_result"
Cohesion: 0.29
Nodes (7): vban_seq_result, VBAN_SEQ_FIRST, VBAN_SEQ_GAP, VBAN_SEQ_LATE, VBAN_SEQ_OK, VBAN_SEQ_RESET, VBAN_SEQ_SUSPECT

### Community 24 - "run"
Cohesion: 0.14
Nodes (6): null_send(), gauss(), main(), urand(), smoke_send(), app_main()

### Community 25 - "JitterBuffer"
Cohesion: 0.29
Nodes (3): JitterBuffer, jb_, mem_

### Community 26 - "vban_seq"
Cohesion: 0.06
Nodes (30): ADR-001: Playout-deadline jitter buffer, then optional loss repair (FEC), Alternatives Considered, Baseline at acceptance (netsim, 120 s, 64-frame packets, 128-frame period), Consequences, Context, Decision (proposed), Dependencies, Outcome (implemented 2026-11-12) (+22 more)

### Community 27 - "vban_rtp_client"
Cohesion: 0.50
Nodes (4): vban_rtp_client, addr, expire_ns, ids

### Community 28 - "vban_reasm"
Cohesion: 0.25
Nodes (8): vban_reasm, active, buf, cap, dropped, frame, len, next

### Community 29 - "vban_rx_config"
Cohesion: 0.22
Nodes (9): vban_rx_config, identity, on_service, on_unmatched, parse_flags, ping_full_to_public, send, send_ctx (+1 more)

### Community 30 - "jobs"
Cohesion: 0.13
Nodes (14): concurrency, cancel-in-progress, group, jobs, windows-arm64-cross, name, on, pull_request (+6 more)

### Community 31 - "matrix"
Cohesion: 0.16
Nodes (14): container, name, runs-on, strategy, esp-idf, linux-cross, name, runs-on (+6 more)

### Community 32 - "run-local.sh"
Cohesion: 0.29
Nodes (11): cmake_test(), have(), run_stage(), run-local.sh script, skip(), st_aarch64(), st_arduino(), st_asan() (+3 more)

### Community 33 - "steps"
Cohesion: 0.31
Nodes (11): steps, steps, steps, steps, if, name, run, shell (+3 more)

### Community 34 - "include"
Cohesion: 0.22
Nodes (9): arch, args, fqbn, name, os, platforms, qemu, triple (+1 more)

### Community 35 - "with"
Cohesion: 0.22
Nodes (9): with, cli-compile-flags, fqbn, libraries, msystem, pacboy, platforms, sketch-paths (+1 more)

### Community 37 - "vban_header"
Cohesion: 0.29
Nodes (7): vban_header, format_bit, format_nbc, format_nbs, format_SR, name, nu_frame

### Community 38 - "vban_tx_config"
Cohesion: 0.13
Nodes (15): vban_send_fn, vban_out, dest, frame, name, send, send_ctx, vban_tx_config (+7 more)

### Community 39 - "vban.hpp"
Cohesion: 0.24
Nodes (4): Addr, addr(), check(), Net

### Community 40 - "mingw"
Cohesion: 0.33
Nodes (6): run, mingw, defaults, name, runs-on, shell

### Community 41 - "Security — vban-sdk"
Cohesion: 0.40
Nodes (4): Findings (independent audit, verified with ASan/UBSan PoC) — all FIXED, see table below, Fixes (2026-11-12) — verified by tests (399 checks, ASan/UBSan clean) and by re-running the auditor's PoC, Security — vban-sdk, Threat model (confirmed with user)

### Community 42 - "Handoff"
Cohesion: 0.50
Nodes (3): Current Handoff, Handoff, Handoff History

### Community 43 - "arduino"
Cohesion: 0.40
Nodes (5): name, runs-on, steps, strategy, arduino

### Community 44 - "native"
Cohesion: 0.50
Nodes (4): native, name, runs-on, strategy

### Community 45 - "windows-arm64-native"
Cohesion: 0.50
Nodes (4): windows-arm64-native, if, name, runs-on

### Community 46 - "vban_jb_anchor"
Cohesion: 0.33
Nodes (6): vban_jb_anchor, epoch, frame, nbs, pos, valid

### Community 65 - "vban_rx_stats"
Cohesion: 0.29
Nodes (7): vban_rx_stats, invalid, packets, pings_answered, pings_limited, pings_refused, unmatched

### Community 66 - "vban_msg"
Cohesion: 0.40
Nodes (5): vban_msg, addr, buf, cap, len

### Community 69 - "vban_pacer"
Cohesion: 0.50
Nodes (4): vban_pacer, frames, rate, start_ns

### Community 70 - "vban_addr"
Cohesion: 0.67
Nodes (3): vban_addr, ip, port

## Knowledge Gaps
- **284 isolated node(s):** `s_`, `pkts`, `channels`, `datatype`, `dest` (+279 more)
  These have ≤1 connection - possible missing edges or undocumented components. (Counts symbols only; 438 node(s) total have ≤1 connection when file, concept and rationale nodes are included.)
- **20 thin communities (<3 nodes) omitted from report** — run `graphify query` to explore isolated nodes.

## Suggested Questions
_Questions this graph is uniquely positioned to answer:_

- **Why does `vban_jb` connect `vban_jb` to `test_main.c`, `vban_proto.c`?**
  _High betweenness centrality (0.102) - this node is a cross-community bridge._
- **Are the 34 inferred relationships involving `main()` (e.g. with `vban_jb_init()` and `vban_jb_pull_f32()`) actually correct?**
  _`main()` has 34 INFERRED edges - model-reasoned connections that need verification._
- **What connects `s_`, `pkts`, `channels` to the rest of the system?**
  _284 weakly-connected nodes found - possible documentation gaps or missing edges._
- **Should `main` be split into smaller, more focused modules?**
  _Cohesion score 0.05772005772005772 - nodes in this community are weakly interconnected._
- **Why does `vban_addr_t` connect `main` to `test_cpp.cpp`, `vban_proto.c`, `vban_msg`, `vban_tx_config`, `vban_tx`, `vban_proto.h`, `vban_heard`, `vban_tx.c`, `run`, `vban_rtp_client`?**
  _High betweenness centrality (0.064) - this node is a cross-community bridge._
- **Are the 27 inferred relationships involving `main()` (e.g. with `arg_flag()` and `arg_int()`) actually correct?**
  _`main()` has 27 INFERRED edges - model-reasoned connections that need verification._
- **Should `vban_proto.c` be split into smaller, more focused modules?**
  _Cohesion score 0.06481481481481481 - nodes in this community are weakly interconnected._