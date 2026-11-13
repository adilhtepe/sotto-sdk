# Graph Report - SDK  (2026-11-13)

## Corpus Check
- 96 files · ~467,823 words
- Verdict: corpus is large enough that graph structure adds value.
- Unclassified: 23 file(s) not represented in the graph (top: (none) 7, .properties 3, .bat 2)

## Summary
- 5518 nodes · 20504 edges · 167 communities (142 shown, 25 thin omitted)
- Extraction: 96% EXTRACTED · 4% INFERRED · 0% AMBIGUOUS · INFERRED: 857 edges (avg confidence: 0.85)
- Token cost: 0 input · 0 output

## Graph Freshness
- Built from commit: `93eae1d3`
- Run `git rev-parse HEAD` and compare to check if the graph is stale.
- Run `graphify update .` after code changes (no API cost).

## Community Hubs (Navigation)
- vban_jitter.c
- Jitter Buffer State
- Mixer & C++ Wrapper
- vban_addr_t
- vban_net.c
- vban_mixer
- JNI Glue
- PCM Conversion
- vban_seq_result
- miniaudio.h
- main
- Kotlin JVM Tests
- ma_uint64
- MA_API
- PING0 Identity Struct
- vban_proto.h
- Docs: Android API
- Kotlin Native Bridge
- ma_result
- CI Windows Jobs
- CI Job Matrix
- Parsed Packet View
- vban_tx
- Benchmarks & Build Targets
- Jitter Buffer Stats
- MA_INLINE
- ma_sound
- Local CI Runner
- CI Arduino Jobs
- Error Codes
- Sequence Tracker State
- Netsim & Redundancy Docs
- CI Workflow Root
- ma_dither_mode
- Kotlin Receiver
- Kotlin Sender
- ma_log_postf
- Android Build & Tests Docs
- Jitter Buffer Config
- ma_device_config
- Android Network Locks
- Kernel Driver Docs
- CI Cross Matrix
- ma_dr_mp3
- ma_device_init
- Kotlin Text Commands
- Memory Footprint Docs
- Reassembly Buffer
- .discover
- CI MinGW Jobs
- ma_bool32
- Spec Interpretation Docs
- Native Handle Guard
- Realtime Thread Boost
- vban_rx_stream
- vban_heard
- CI ESP-IDF Job
- Float Safety Policy
- vban_noise.c
- ma_allocation_callbacks
- Kernel Capture Fix
- Kernel VM Test
- Config Limits Doc
- ma_int16
- ma_uint32
- ma_context
- CMSVADHW
- ma_strncpy_s
- ma_spatializer_process_pcm_frames
- ma_node
- ma_decoder
- vban_proto.c
- ma_format
- vban_secure.c
- vban_sec_server
- ma_engine_node_process_pcm_frames__general
- ma_dr_flac
- run
- CSaveData
- ma_vfs
- SottoAudio.c
- ma_get_bytes_per_frame
- vban_sec_link
- CMiniportWaveCyclic
- vban_cert
- ma_resource_manager
- ma_engine
- ma_IAudioClient3
- ma_data_source
- ma_malloc
- LICENCE.md
- ma_node_graph
- ma_uint8
- ma_hpf_reinit__internal
- ma_dr_flac_open_with_metadata_private
- ma_pcm_rb
- ma_waveform
- ma_lpf_reinit__internal
- CIVSHMEMSaveData
- CMiniportWaveCyclicStream
- mintopo.cpp
- ma_dr_wav__metadata_process_chunk
- type
- ma_resource_manager_data_buffer
- ma_device_info
- DWORD
- IVSHMEM_OBJECT
- ivshmemsavedata.h
- minstream.cpp
- ma_resampler_config
- ma_audio_buffer
- ma_encoder
- ma_linear_resampler
- ma_resource_manager_data_stream
- ma_data_source_get_data_format
- ma_job_queue_next
- main
- ma_biquad_init_preallocated
- ma_bpf_reinit__internal
- ma_channel_converter_init_preallocated
- ma_job
- ma_noise
- ma_completion_handler_uwp
- ma_dr_flac_oggbs__goto_next_page
- ma_IDirectSoundCaptureBuffer
- ma_dr_mp3__full_read_and_close_s16
- ma_resource_manager_data_buffer_init_ex_internal
- scream.h
- ma_get_bytes_per_sample
- vban_sec_session
- ma_sio_par
- vban_noise_hs
- wchar_t
- ma_panner
- ma_IDirectSound
- IID
- vban_cert.c
- ma_semaphore
- ma_audio_worklet_processor_created__webaudio
- tagWAVEHDR
- ma_IPropertyStore
- SottoVBAN/common.h
- ma_SLDataFormat_PCM_init__opensl
- ma_thread_wait
- ma_IDirectSoundNotify
- vban_sec_id
- ma_sio_cap
- SottoAudio: virtual sound devices for macOS
- Sotto VBAN: virtual sound card for Windows
- ma_data_source_set_looping
- container-test.sh
- ma_to_recording_preset__opensl
- ma_sio_conf
- ma_paged_audio_buffer_data_allocate_page
- sotto/vm-test.sh
- ma_to_AVAudioSessionCategory
- ma_pthread_cond_t
- ma_pthread_mutex_t

## God Nodes (most connected - your core abstractions)
1. `ma_uint64` - 652 edges
2. `ma_uint32` - 551 edges
3. `ma_result` - 532 edges
4. `ma_allocation_callbacks` - 251 edges
5. `ma_device` - 188 edges
6. `ma_context` - 167 edges
7. `ma_free()` - 145 edges
8. `ma_bool32` - 138 edges
9. `ma_int32` - 129 edges
10. `ma_uint8` - 124 edges

## Surprising Connections (you probably didn't know these)
- `main()` --calls--> `vban_sec_pubkey()`  [INFERRED]
  examples/vban_cert.c → src/vban_secure.c
- `PING0 device names cut to printable ASCII` --semantically_similar_to--> `SEC-007..009 truncation, ANSI injection, caller misuse`  [INFERRED] [semantically similar]
  android/README.md → README.md
- `Kernel receive thread with frame-counter ring` --semantically_similar_to--> `Playout-deadline jitter buffer (ADR-001)`  [INFERRED] [semantically similar]
  kernel/README.md → README.md
- `RAM footprint (no heap)` --semantically_similar_to--> `Memory footprint (SDK never calls malloc)`  [INFERRED] [semantically similar]
  bench/RESULTS_x86_64.txt → README.md
- `now_ms()` --calls--> `vban_time_ns()`  [INFERRED]
  apps/sotto-pw/sotto_pw.c → src/vban_net.c

## Import Cycles
- None detected.

## Hyperedges (group relationships)
- **VBAN SDK core C modules** — readme_vban_proto_module, readme_vban_pcm_module, readme_vban_jitter_module, readme_vban_mixer_module, readme_vban_rx_module, readme_vban_tx_module, readme_vban_net_module [EXTRACTED 1.00]
- **Loss handling: frame-counter placement, redundant copies, playout concealment** — readme_vban_seq, readme_playout_deadline_buffer, readme_redundant_copies, bench_netsim_results_fec_dup4, kernel_readme_receive_thread [INFERRED 0.85]
- **Build targets consuming the vban library** — cmakelists_vban, android_vban_src_main_cpp_cmakelists_vban_jni, ci_esp_idf_cmakelists_vban_idf_smoke, cmakelists_vban_tests, cmakelists_vban_bench, cmakelists_vban_netsim [INFERRED 0.85]

## Communities (167 total, 25 thin omitted)

### Community 0 - "vban_jitter.c"
Cohesion: 0.09
Nodes (43): make_node(), now_ms(), on_in_process(), on_out_process(), sealing_send(), send_cb(), conceal_frame(), copy_out() (+35 more)

### Community 1 - "Jitter Buffer State"
Cohesion: 0.04
Nodes (56): vban_jb, adaptive, aseq, c_cur, c_prev, channels, conc_gain, concealed (+48 more)

### Community 2 - "Mixer & C++ Wrapper"
Cohesion: 0.05
Nodes (36): Addr, Error, code, Mixer, Net, finish_output(), mem_, mx_ (+28 more)

### Community 3 - "vban_addr_t"
Cohesion: 0.08
Nodes (58): on_ping_reply(), net_input(), play_input(), ex_safe_text(), on_any(), on_service(), link_input(), link_tick() (+50 more)

### Community 4 - "vban_net.c"
Cohesion: 0.04
Nodes (30): clock_thread(), do_capture(), do_playback(), open_sockets(), parse_dest(), rx_pull(), snd_vban_exit(), stream_of() (+22 more)

### Community 5 - "vban_mixer"
Cohesion: 0.14
Nodes (14): vban_mixer, cur, dirty, in_gain, in_mute, limiter, master, master_mute (+6 more)

### Community 6 - "JNI Glue"
Cohesion: 0.19
Nodes (29): get_str(), Java_vban_sdk_VbanNative_cmdCreate(), Java_vban_sdk_VbanNative_cmdDestroy(), Java_vban_sdk_VbanNative_cmdSendText(), Java_vban_sdk_VbanNative_discover(), Java_vban_sdk_VbanNative_rxCreate(), Java_vban_sdk_VbanNative_rxDestroy(), Java_vban_sdk_VbanNative_rxLocalPort() (+21 more)

### Community 7 - "PCM Conversion"
Cohesion: 0.17
Nodes (36): clampf(), f2i(), f2i32(), f2i_s24(), f32_bits_san(), f32_to_s24(), f32_wire_to_f32(), f32_wire_to_s16() (+28 more)

### Community 8 - "vban_seq_result"
Cohesion: 0.29
Nodes (7): vban_seq_result, VBAN_SEQ_FIRST, VBAN_SEQ_GAP, VBAN_SEQ_LATE, VBAN_SEQ_OK, VBAN_SEQ_RESET, VBAN_SEQ_SUSPECT

### Community 9 - "miniaudio.h"
Cohesion: 0.01
Nodes (160): ma_AAudioStream_t, ma_AAudioStreamBuilder_t, ma_atomic_is_lock_free_16(), ma_atomic_is_lock_free_8(), ma_attenuation_exponential(), ma_biquad_coefficient, ma_biquad_config ma_bpf2__get_biquad_config(), ma_biquad_config ma_hishelf2__get_biquad_config() (+152 more)

### Community 10 - "main"
Cohesion: 0.07
Nodes (70): main(), parse_hostport(), cmd_devices(), cmd_play(), cmd_send(), find_device(), main(), now_ms() (+62 more)

### Community 12 - "ma_uint64"
Cohesion: 0.05
Nodes (141): ma_apply_volume_factor_s24(), ma_copy_and_apply_volume_factor_s24(), ma_dr_wav__bswap64(), ma_dr_wav__bswap_s64(), ma_dr_wav__bswap_samples(), ma_dr_wav__bswap_samples_s64(), ma_dr_wav__chunk_padding_size_riff(), ma_dr_wav__chunk_padding_size_w64() (+133 more)

### Community 13 - "MA_API"
Cohesion: 0.02
Nodes (143): ma_android_sdk_version(), ma_async_notification_event_init(), ma_async_notification_event__on_signal(), ma_async_notification_event_signal(), ma_async_notification_event_uninit(), ma_async_notification_event_wait(), ma_async_notification_poll_init(), ma_async_notification_poll_is_signalled() (+135 more)

### Community 14 - "PING0 Identity Struct"
Cohesion: 0.10
Nodes (20): vban_ping0, application_name, bit_feature, bit_feature_ex, bit_type, color_rgb, device_name, distant_ip (+12 more)

### Community 15 - "vban_proto.h"
Cohesion: 0.03
Nodes (79): get_unaligned_le16(), kring_conceal_frame(), kring_frame_valid(), kring_init(), kring_place(), kring_pull(), vban_seq_init(), vban_seq_update() (+71 more)

### Community 16 - "Docs: Android API"
Cohesion: 0.14
Nodes (16): VbanDiscovery, VbanReceiver, Latency guide, Limits (IPv4 only, no VBCA/VBCV decode, linear resampler), SERVICE PING0 discovery, Protocol coverage table, SERVICE RT-Packet register/server, SEC-001/002 RT-Packet reflector / slot exhaustion (+8 more)

### Community 18 - "ma_result"
Cohesion: 0.04
Nodes (126): EMSCRIPTEN_KEEPALIVE ma_device_process_pcm_frames_capture__webaudio(), EMSCRIPTEN_KEEPALIVE ma_device_process_pcm_frames_playback__webaudio(), ma_calloc(), ma_close_streams__aaudio(), ma_context_init_engine_nolock__opensl(), ma_context_open_device__oss(), ma_decoder_internal_on_read__custom(), ma_decoder_read_bytes() (+118 more)

### Community 19 - "CI Windows Jobs"
Cohesion: 0.16
Nodes (19): steps, steps, windows-arm64-cross, windows-arm64-native, steps, steps, steps, if (+11 more)

### Community 20 - "CI Job Matrix"
Cohesion: 0.13
Nodes (18): name, runs-on, strategy, strategy, android, linux-cross, native, name (+10 more)

### Community 21 - "Parsed Packet View"
Cohesion: 0.11
Nodes (18): vban_packet, channels, codec, data, data_len, datatype, format_bit, format_nbc (+10 more)

### Community 22 - "vban_tx"
Cohesion: 0.04
Nodes (50): vban_send_fn, vban_rtp_client_t, vban_rtp_server, allow_public, clients, frame, n_clients, name (+42 more)

### Community 23 - "Benchmarks & Build Targets"
Cohesion: 0.14
Nodes (15): VBAN SDK 1.0.0 x86_64 benchmark results, End-to-end tx_write to jb_push loopback latency (p50 10 us), vban_idf_smoke ESP-IDF CI project, ESP-IDF main component (main.c, arduino/vban_smoke include), ESP-IDF component mode (ESP_PLATFORM branch), vban static library target, vban_bench executable, VBAN build options (TESTS/EXAMPLES/BENCH/ENABLE_NET/SANITIZE/WERROR) (+7 more)

### Community 24 - "Jitter Buffer Stats"
Cohesion: 0.12
Nodes (16): vban_jb_stats, concealed_frames, dup, fill_frames, late, lost, overruns, packets (+8 more)

### Community 25 - "MA_INLINE"
Cohesion: 0.04
Nodes (119): float32x4_t ma_mix_f32_fast__neon(), ma_atomic_compare_and_swap_16(), ma_atomic_compare_and_swap_32(), ma_atomic_compare_and_swap_64(), ma_atomic_compare_and_swap_8(), ma_atomic_compare_and_swap_f32(), ma_atomic_compare_and_swap_f64(), ma_atomic_compare_and_swap_ptr() (+111 more)

### Community 26 - "ma_sound"
Cohesion: 0.03
Nodes (116): ma_atomic_exchange_explicit_32(), ma_atomic_exchange_explicit_f32(), ma_attenuation_model, ma_attenuation_model ma_sound_get_attenuation_model(), ma_attenuation_model ma_sound_group_get_attenuation_model(), ma_bool8, ma_engine_get_listener_count(), ma_engine_get_sample_rate() (+108 more)

### Community 27 - "Local CI Runner"
Cohesion: 0.29
Nodes (11): cmake_test(), have(), run_stage(), run-local.sh script, skip(), st_aarch64(), st_arduino(), st_asan() (+3 more)

### Community 28 - "CI Arduino Jobs"
Cohesion: 0.15
Nodes (13): name, runs-on, steps, arduino, with, cli-compile-flags, fqbn, libraries (+5 more)

### Community 29 - "Error Codes"
Cohesion: 0.12
Nodes (17): vban_err, VBAN_E_ARG, VBAN_E_AUTH, VBAN_E_CERT, VBAN_E_FORMAT, VBAN_E_MAGIC, VBAN_E_NET, VBAN_E_NOSPACE (+9 more)

### Community 30 - "Sequence Tracker State"
Cohesion: 0.15
Nodes (13): vban_seq, cand, expected, has_cand, has_prev, hist, max_gap, n_hist (+5 more)

### Community 31 - "Netsim & Redundancy Docs"
Cohesion: 0.21
Nodes (8): VbanSender, Netsim fec dup:4 (redundant copies) run, Netsim fec xor:4 (XOR parity) run, Netsim results: wired LAN, Wi-Fi good/congested, WAN/VPN, vban_netsim executable, Kernel receive thread with frame-counter ring, vban_netsim network simulator, vban_tx (zero-copy packetizer, TEXT/SERIAL/MIDI/FRAME/PING senders, pacer)

### Community 32 - "CI Workflow Root"
Cohesion: 0.17
Nodes (11): concurrency, cancel-in-progress, group, jobs, name, on, pull_request, push (+3 more)

### Community 33 - "ma_dither_mode"
Cohesion: 0.05
Nodes (120): ma_copy_memory_64(), ma_cpuid(), ma_dither_f32(), ma_dither_f32_rectangle(), ma_dither_f32_triangle(), ma_dither_mode, ma_dither_s32(), ma_has_avx() (+112 more)

### Community 36 - "ma_log_postf"
Cohesion: 0.04
Nodes (100): ma_add_native_data_format_to_device_info_from_WAVEFORMATEX(), ma_completion_handler_uwp_init(), ma_config_to_WAVEFORMATEXTENSIBLE(), ma_context_create_IDirectSound__dsound(), ma_context_create_IDirectSoundCapture__dsound(), ma_context_create_IMMDeviceEnumerator__wasapi(), ma_context_enumerate_devices_by_type__wasapi(), ma_context_enumerate_devices__wasapi() (+92 more)

### Community 37 - "Android Build & Tests Docs"
Cohesion: 0.24
Nodes (7): Composite build integration (includeBuild, vban.sdk:vban:1.0.0), Kotlin JVM unit tests (testDebugUnitTest, 9/9 pass), VBAN SDK for Android (Kotlin library vban.sdk), VbanException (VBAN_E_* codes), VbanNetworkLocks (MulticastLock + low-latency WifiLock), vban_jni shared library, Android notes (MulticastLock, WifiLock, AAudio/Oboe, nice -19)

### Community 38 - "Jitter Buffer Config"
Cohesion: 0.20
Nodes (10): vban_jb_config, adaptive, channels, drift, max_frames, max_gap, min_frames, out_rate (+2 more)

### Community 39 - "ma_device_config"
Cohesion: 0.04
Nodes (69): ma_best_format_from_fd__audio4(), ma_calculate_buffer_size_in_frames_from_descriptor(), ma_calculate_buffer_size_in_frames_from_milliseconds(), ma_calculate_period_size_in_frames_from_descriptor__dsound(), ma_calculate_period_size_in_frames_from_descriptor__pulse(), ma_calculate_period_size_in_frames_from_descriptor__webaudio(), ma_calculate_period_size_in_frames_from_descriptor__winmm(), ma_channel_map_init_blank() (+61 more)

### Community 41 - "Kernel Driver Docs"
Cohesion: 0.22
Nodes (8): Example tool executables (vban_send, vban_recv, vban_ping, vban_text, vban_mixer_demo, vban_monitor), SCHED_FIFO hrtimer clock thread, snd-vban module parameters (dest, tx_name, listen_port, rx_name, rate, fpp, rx_target), Bug: example tools ignored SIGTERM (fixed), snd-vban ALSA virtual sound card driver, vm-test.sh QEMU/virtme-ng tests, Example tools (examples/), OpenWRT-VBAN-Hub (vbanhub) interop

### Community 42 - "CI Cross Matrix"
Cohesion: 0.22
Nodes (9): arch, args, fqbn, name, os, platforms, qemu, triple (+1 more)

### Community 43 - "ma_dr_mp3"
Cohesion: 0.05
Nodes (72): ma_dr_mp3_bind_seek_table(), ma_dr_mp3_bs_get_bits(), ma_dr_mp3_bs_init(), ma_dr_mp3_calculate_seek_points(), ma_dr_mp3_clip_int16_arm(), ma_dr_mp3_decode_next_frame(), ma_dr_mp3_decode_next_frame_ex(), ma_dr_mp3_decode_next_frame_ex__callbacks() (+64 more)

### Community 44 - "ma_device_init"
Cohesion: 0.05
Nodes (71): EMSCRIPTEN_KEEPALIVE ma_device__on_notification_unlocked(), ma_context_enumerate_devices(), ma_context_get_device_info(), ma_context_get_devices(), ma_context_get_devices__enum_callback(), ma_context_init_command__wasapi(), ma_context_is_backend_asynchronous(), ma_context_next_command__wasapi() (+63 more)

### Community 46 - "Memory Footprint Docs"
Cohesion: 0.25
Nodes (7): RAM footprint (no heap), vban_cpp_test executable, Memory footprint (SDK never calls malloc), vban.hpp header-only C++11 RAII wrapper, vban_mixer (N x M matrix mixer, ramps, limiter), vban_net (UDP POSIX/Winsock, recvmmsg/sendmmsg, clock, RT priority), VBAN SDK

### Community 47 - "Reassembly Buffer"
Cohesion: 0.25
Nodes (8): vban_reasm, active, buf, cap, dropped, frame, len, next

### Community 49 - "CI MinGW Jobs"
Cohesion: 0.29
Nodes (7): run, mingw, defaults, name, runs-on, strategy, shell

### Community 50 - "ma_bool32"
Cohesion: 0.07
Nodes (73): ma_apply_volume_factor_s32(), ma_bool32, ma_copy_and_apply_volume_factor_s32(), ma_dr_flac__calculate_prediction_32(), ma_dr_flac__calculate_prediction_64(), ma_dr_flac__clz(), ma_dr_flac__clz_msvc(), ma_dr_flac__clz_software() (+65 more)

### Community 51 - "Spec Interpretation Docs"
Cohesion: 0.33
Nodes (5): VbanCommand, VBAN_SOURCES (seven src/*.c files), shim/ mapping userspace headers to kernel headers (integer-only), vban_proto (wire format, parse/build, PING0, sequencing), Voicemeeter

### Community 52 - "Native Handle Guard"
Cohesion: 0.22
Nodes (3): NativeHandle, VbanException, VbanFormat

### Community 54 - "vban_rx_stream"
Cohesion: 0.10
Nodes (21): vban_rx_subscribe(), vban_rx_cb, vban_rx_config, identity, on_service, on_unmatched, parse_flags, ping_full_to_public (+13 more)

### Community 55 - "vban_heard"
Cohesion: 0.22
Nodes (9): vban_heard, format_bit, format_nbc, format_SR, from, last_ns, name, packets (+1 more)

### Community 56 - "CI ESP-IDF Job"
Cohesion: 0.40
Nodes (5): container, name, runs-on, strategy, esp-idf

### Community 60 - "vban_noise.c"
Cohesion: 0.31
Nodes (18): decrypt_and_hash(), dh(), encrypt_and_hash(), hkdf2(), hmac(), mix_dh(), mix_hash(), mix_key() (+10 more)

### Community 61 - "ma_allocation_callbacks"
Cohesion: 0.05
Nodes (73): EMSCRIPTEN_KEEPALIVE ma_free_emscripten(), ma_aligned_free(), ma_aligned_malloc(), ma_allocation_callbacks, ma_allocation_callbacks_init_copy(), ma_allocation_callbacks_init_default(), ma_allocation_callbacks ma_dr_wav_copy_allocation_callbacks_or_defaults(), ma_audio_buffer_config ma_audio_buffer_config_init() (+65 more)

### Community 68 - "ma_int16"
Cohesion: 0.04
Nodes (75): ma_apply_volume_factor_pcm_frames_s16(), ma_apply_volume_factor_s16(), ma_apply_volume_unclipped_f32(), ma_apply_volume_unclipped_s16(), ma_apply_volume_unclipped_s24(), ma_apply_volume_unclipped_s32(), ma_apply_volume_unclipped_u8(), ma_clip_f32() (+67 more)

### Community 69 - "ma_uint32"
Cohesion: 0.04
Nodes (75): ma_apply_volume_factor_pcm_frames(), ma_apply_volume_factor_pcm_frames_f32(), ma_apply_volume_factor_pcm_frames_s24(), ma_apply_volume_factor_pcm_frames_s32(), ma_convert_frames(), ma_copy_and_apply_volume_factor_pcm_frames(), ma_copy_and_apply_volume_factor_pcm_frames_f32(), ma_copy_and_apply_volume_factor_pcm_frames_s24() (+67 more)

### Community 70 - "ma_context"
Cohesion: 0.06
Nodes (76): ma_backend_callbacks, ma_context, allocationCallbacks, backend, callbacks, captureDeviceInfoCount, ma_context_config, allocationCallbacks (+68 more)

### Community 71 - "CMSVADHW"
Cohesion: 0.05
Nodes (22): CAdapterCommon, m_pDeviceObject, m_pHW, m_PowerState, m_pPortWave, m_pServiceGroupWave, CAdapterCommon::PowerChangeState(), CAdapterCommon::QueryDeviceCapabilities() (+14 more)

### Community 72 - "ma_strncpy_s"
Cohesion: 0.05
Nodes (62): ma_construct_device_id__audio4(), ma_context_enumerate_devices__aaudio(), ma_context_enumerate_devices__alsa(), ma_context_enumerate_devices__audio4(), ma_context_enumerate_devices__dsound(), ma_context_enumerate_devices__jack(), ma_context_enumerate_devices__null(), ma_context_enumerate_devices__opensl() (+54 more)

### Community 73 - "ma_spatializer_process_pcm_frames"
Cohesion: 0.07
Nodes (70): ma_atomic_vec3f, ma_atomic_vec3f_get(), ma_atomic_vec3f_init(), ma_atomic_vec3f_set(), ma_attenuation_inverse(), ma_attenuation_linear(), ma_attenuation_model ma_spatializer_get_attenuation_model(), ma_calculate_angular_gain() (+62 more)

### Community 74 - "ma_node"
Cohesion: 0.07
Nodes (63): ma_context_uninit__opensl(), ma_engine_read_pcm_frames(), ma_engine_uninit(), ma_node_attach_output_bus(), ma_node_config_get_cache_size_in_frames(), ma_node_detach_all_output_buses(), ma_node_detach_full(), ma_node_detach_output_bus() (+55 more)

### Community 75 - "ma_decoder"
Cohesion: 0.08
Nodes (62): ma_data_source_base, ma_decode_file(), ma_decode_from_vfs(), ma_decode_memory(), ma_decoder, allocationCallbacks, ma_decoder_config ma_decoder_config_init_copy(), converter (+54 more)

### Community 76 - "vban_proto.c"
Cohesion: 0.08
Nodes (56): rx_thread(), snd_vban_init(), vban_f32_deinterleave(), vban_f32_interleave(), build_data(), lower_ascii(), parse_body(), put_header() (+48 more)

### Community 77 - "ma_format"
Cohesion: 0.05
Nodes (60): ma_audio_buffer_ref__data_source_on_get_data_format(), ma_biquad_config ma_biquad_config_init(), ma_bpf2_config ma_bpf2_config_init(), ma_bpf_config ma_bpf_config_init(), ma_channel, ma_channel_converter_get_input_channel_map(), ma_channel_from_AudioChannelBitmap(), ma_channel_from_AudioChannelLabel() (+52 more)

### Community 78 - "vban_secure.c"
Cohesion: 0.14
Nodes (33): random_serial(), b64v(), cookie_ok(), find(), get32(), get64(), go_up(), hdr() (+25 more)

### Community 79 - "vban_sec_server"
Cohesion: 0.14
Nodes (14): vban_sec_peer_t, vban_sec_server, cookie_key, cookie_ms, cookie_prev, cookie_tokens, hs_cookies, hs_evicted (+6 more)

### Community 80 - "ma_engine_node_process_pcm_frames__general"
Cohesion: 0.05
Nodes (58): ma_apply_volume_factor_f32(), ma_copy_and_apply_volume_factor_f32(), ma_engine_get_channels(), ma_engine_node_base_node_config_init(), ma_engine_node_get_heap_layout(), ma_engine_node_get_heap_size(), ma_engine_node_get_required_input_frame_count(), ma_engine_node_get_required_input_frame_count__group() (+50 more)

### Community 81 - "ma_dr_flac"
Cohesion: 0.08
Nodes (58): ma_dr_flac__decode_flac_frame(), ma_dr_flac__decode_flac_frame_and_seek_forward_by_pcm_frames(), ma_dr_flac__get_channel_count_from_channel_assignment(), ma_dr_flac__get_pcm_frame_range_of_current_flac_frame(), ma_dr_flac_ogg__seek_to_pcm_frame(), ma_dr_flac__read_and_decode_next_flac_frame(), ma_dr_flac_read_pcm_frames_f32(), ma_dr_flac_read_pcm_frames_f32__decode_independent_stereo() (+50 more)

### Community 82 - "run"
Cohesion: 0.12
Nodes (8): null_send(), gauss(), main(), run(), urand(), smoke_send(), app_main(), main()

### Community 83 - "CSaveData"
Cohesion: 0.04
Nodes (26): ControlSocketComplete(), CSaveData::SendData(), CSaveData, m_bVbanBit, m_bVbanNbc, m_bVbanNbs, m_bVbanSr, m_fWriteDisabled (+18 more)

### Community 84 - "ma_vfs"
Cohesion: 0.10
Nodes (55): ma_decoder_internal_on_seek__custom(), ma_decoder_internal_on_tell__custom(), ma_decoder__on_read_vfs(), ma_decoder__on_seek_memory(), ma_decoder__on_seek_vfs(), ma_decoder__on_tell_vfs(), ma_decoder_seek_bytes(), ma_decoder_tell_bytes() (+47 more)

### Community 85 - "SottoAudio.c"
Cohesion: 0.12
Nodes (30): AbortDeviceConfigurationChange(), AddDeviceClient(), AddRef(), BeginIOOperation(), CreateDevice(), DestroyDevice(), dev_of(), DoIOOperation() (+22 more)

### Community 86 - "ma_get_bytes_per_frame"
Cohesion: 0.08
Nodes (53): ma_buffer_queue_callback_capture__opensl_android(), ma_buffer_queue_callback_playback__opensl_android(), ma_channel_converter, ma_channel_converter_process_pcm_frames(), ma_channel_converter_process_pcm_frames__mono_in(), ma_channel_converter_process_pcm_frames__mono_out(), ma_channel_converter_process_pcm_frames__passthrough(), ma_channel_converter_process_pcm_frames__weights() (+45 more)

### Community 87 - "vban_sec_link"
Cohesion: 0.15
Nodes (13): vban_sec_link, cookie_used, cur, hs1, last_try_ms, last_tx_ms, next, prev_bits (+5 more)

### Community 88 - "CMiniportWaveCyclic"
Cohesion: 0.06
Nodes (32): CMiniportWaveCyclic, CMiniportWaveCyclic::CMiniportWaveCyclic(), CMiniportWaveCyclic::DataRangeIntersection(), CMiniportWaveCyclic::GetDescription(), CMiniportWaveCyclic::Init(), m_AdapterCommon, m_fCaptureAllocated, m_FilterDescriptor (+24 more)

### Community 89 - "vban_cert"
Cohesion: 0.22
Nodes (9): vban_cert, issuer, key, name, not_after, not_before, role, serial (+1 more)

### Community 90 - "ma_resource_manager"
Cohesion: 0.09
Nodes (48): ma_decoder_uninit(), ma_hash_string_32(), ma_hash_string_w_32(), ma_job_process__resource_manager__load_data_buffer_node(), ma_resource_manager, config, ma_resource_manager_data_buffer_bst_lock(), ma_resource_manager_data_buffer_bst_unlock() (+40 more)

### Community 92 - "ma_engine"
Cohesion: 0.05
Nodes (46): ma_engine, allocationCallbacks, defaultVolumeSmoothTimeInPCMFrames, gainSmoothTimeInFrames, ma_engine_get_device(), ma_engine_get_endpoint(), ma_engine_get_gain_db(), ma_engine_get_log() (+38 more)

### Community 93 - "ma_IAudioClient3"
Cohesion: 0.07
Nodes (38): ma_context_enumerate_devices_callback__dsound(), ma_IAudioClient2, ma_IAudioClient2_AddRef(), ma_IAudioClient2_GetBufferSizeLimits(), ma_IAudioClient2_GetDevicePeriod(), ma_IAudioClient2_GetMixFormat(), ma_IAudioClient2_GetStreamLatency(), ma_IAudioClient2_Initialize() (+30 more)

### Community 94 - "ma_data_source"
Cohesion: 0.06
Nodes (46): ma_audio_buffer_ref__data_source_on_get_cursor(), ma_audio_buffer_ref__data_source_on_get_length(), ma_data_source, ma_data_source_get_next_proc ma_data_source_get_next_callback(), ma_data_source_node_config ma_data_source_node_config_init(), ma_data_source_set_current(), ma_data_source_set_next(), ma_decoder__data_source_on_read() (+38 more)

### Community 95 - "ma_malloc"
Cohesion: 0.12
Nodes (39): EMSCRIPTEN_KEEPALIVE ma_malloc_emscripten(), ma_channel_converter_init(), ma_copy_string(), ma_decoding_backend_init_file__flac(), ma_decoding_backend_init_file__mp3(), ma_decoding_backend_init_file__stbvorbis(), ma_decoding_backend_init_file_w__flac(), ma_decoding_backend_init_file_w__mp3() (+31 more)

### Community 99 - "ma_node_graph"
Cohesion: 0.06
Nodes (31): ma_biquad_node_init(), ma_biquad_node_uninit(), ma_data_source_node_init(), ma_data_source_node_uninit(), ma_delay_node_init(), ma_delay_node_uninit(), ma_delay_uninit(), ma_hishelf_node_init() (+23 more)

### Community 100 - "ma_uint8"
Cohesion: 0.08
Nodes (44): ma_apply_volume_factor_pcm_frames_u8(), ma_apply_volume_factor_u8(), ma_channel_converter_process_pcm_frames__shuffle(), ma_channel_map_apply_shuffle_table(), ma_channel_map_apply_shuffle_table_f32(), ma_channel_map_apply_shuffle_table_s16(), ma_channel_map_apply_shuffle_table_s24(), ma_channel_map_apply_shuffle_table_s32() (+36 more)

### Community 101 - "ma_hpf_reinit__internal"
Cohesion: 0.09
Nodes (41): ma_biquad_float_to_fp(), ma_expd(), ma_hpf, ma_hpf1, ma_hpf1_config, ma_hpf1_get_heap_layout(), ma_hpf1_get_heap_size(), ma_hpf1_get_latency() (+33 more)

### Community 102 - "ma_dr_flac_open_with_metadata_private"
Cohesion: 0.11
Nodes (34): ma_dr_flac__be2host_16(), ma_dr_flac__be2host_32(), ma_dr_flac__be2host_32_ptr_unaligned(), ma_dr_flac__be2host_64(), ma_dr_flac_close(), ma_dr_flac__cpuid(), ma_dr_flac__decode_block_header(), ma_dr_flac__free_from_callbacks() (+26 more)

### Community 103 - "ma_pcm_rb"
Cohesion: 0.10
Nodes (40): ma_device__handle_duplex_callback_capture(), ma_device__handle_duplex_callback_playback(), ma_duplex_rb_init(), ma_pcm_rb_acquire_read(), ma_pcm_rb_acquire_write(), ma_pcm_rb_available_read(), ma_pcm_rb_available_write(), ma_pcm_rb_commit_read() (+32 more)

### Community 104 - "ma_waveform"
Cohesion: 0.08
Nodes (35): ma_debug_fill_pcm_frames_with_sine_wave(), ma_pcm_sample_f32_to_s16(), ma_pulsewave_init(), ma_pulsewave_read_pcm_frames(), ma_pulsewave_seek_to_pcm_frame(), ma_pulsewave_set_amplitude(), ma_pulsewave_set_duty_cycle(), ma_pulsewave_set_frequency() (+27 more)

### Community 105 - "ma_lpf_reinit__internal"
Cohesion: 0.10
Nodes (35): ma_lpf, ma_lpf1, ma_lpf1_config, ma_lpf1_get_heap_layout(), ma_lpf1_get_heap_size(), ma_lpf1_get_latency(), ma_lpf1_init(), ma_lpf1_init_preallocated() (+27 more)

### Community 106 - "CIVSHMEMSaveData"
Cohesion: 0.08
Nodes (14): CAdapterCommon::~CAdapterCommon(), CIVSHMEMSaveData, CIVSHMEMSaveData::CIVSHMEMSaveData(), m_fWriteDisabled, m_ivshmem, m_pBuffer, m_pDeviceObject, m_pWorkItem (+6 more)

### Community 107 - "CMiniportWaveCyclicStream"
Cohesion: 0.05
Nodes (22): CMiniportWaveCyclicStream, m_bChannels, m_bitsPerSample, m_fCapture, m_fDmaActive, m_fFormat16Bit, m_IVSHMEMSaveData, m_ksState (+14 more)

### Community 108 - "mintopo.cpp"
Cohesion: 0.14
Nodes (13): PropertyHandler_BasicSupport(), ValidatePropertyParams(), CMiniportTopology, CMiniportTopology::CMiniportTopology(), CMiniportTopology::DataRangeIntersection(), CMiniportTopology::GetDescription(), m_AdapterCommon, m_FilterDescriptor (+5 more)

### Community 109 - "ma_dr_wav__metadata_process_chunk"
Cohesion: 0.15
Nodes (30): ma_dr_wav_buffer_reader_init(), ma_dr_wav_buffer_reader_ptr(), ma_dr_wav_buffer_reader_read(), ma_dr_wav_buffer_reader_read_u16(), ma_dr_wav_buffer_reader_read_u32(), ma_dr_wav_buffer_reader_seek(), ma_dr_wav_bytes_to_f32(), ma_dr_wav_bytes_to_s32() (+22 more)

### Community 110 - "type"
Cohesion: 0.14
Nodes (28): ma_context_enumerate_devices__coreaudio(), ma_count_set_bits(), ma_device_init__coreaudio(), ma_device_init_internal__coreaudio(), ma_device_reinit_internal__coreaudio(), type, ma_does_AudioObject_support_capture(), ma_does_AudioObject_support_playback() (+20 more)

### Community 111 - "ma_resource_manager_data_buffer"
Cohesion: 0.11
Nodes (34): ma_paged_audio_buffer_get_cursor_in_pcm_frames(), ma_resource_manager_data_buffer, ma_resource_manager_data_buffer_get_available_frames(), ma_resource_manager_data_buffer_get_connector(), ma_resource_manager_data_buffer_get_cursor_in_pcm_frames(), ma_resource_manager_data_buffer_get_data_format(), ma_resource_manager_data_buffer_get_length_in_pcm_frames(), ma_resource_manager_data_buffer_has_connector() (+26 more)

### Community 112 - "ma_device_info"
Cohesion: 0.10
Nodes (25): ma_AVAudioSessionPortDescription_to_device_info(), ma_close_stream__aaudio(), ma_context_add_data_format_ex__opensl(), ma_context_add_data_format__opensl(), ma_context_add_native_data_format_from_AAudioStream__aaudio(), ma_context_add_native_data_format_from_AAudioStream_ex__aaudio(), ma_context_add_native_data_format__oss(), ma_context_enumerate_devices__winmm() (+17 more)

### Community 113 - "DWORD"
Cohesion: 0.11
Nodes (28): ma_channel_id_to_ma__win32(), ma_channel_id_to_win32(), ma_channel_map_to_channel_mask__win32(), ma_channel_mask_to_channel_map__win32(), ma_device_data_loop__dsound(), ma_IDirectSoundBuffer, ma_IDirectSoundBuffer_AddRef(), ma_IDirectSoundBuffer_GetCaps() (+20 more)

### Community 114 - "IVSHMEM_OBJECT"
Cohesion: 0.07
Nodes (24): IVSHMEM_MMAP, peerID, ptr, size, vectors, IVSHMEM_OBJECT, bufferSize, chunkSize (+16 more)

### Community 115 - "ivshmemsavedata.h"
Cohesion: 0.08
Nodes (20): IVSHMEM_SAVEWORKER_PARAM, _IVSHMEM_SAVEWORKER_PARAM, EventDone, pSaveData, WorkItem, PCIVSHMEMSaveData, PIVSHMEM_MMAP, PIVSHMEM_OBJECT (+12 more)

### Community 116 - "minstream.cpp"
Cohesion: 0.10
Nodes (12): GetWaveFormatEx(), CMiniportWaveCyclicStream::CMiniportWaveCyclicStream(), CMiniportWaveCyclicStream::CopyFrom(), CMiniportWaveCyclicStream::CopyTo(), CMiniportWaveCyclicStream::GetPosition(), CMiniportWaveCyclicStream::NonDelegatingQueryInterface(), CMiniportWaveCyclicStream::NormalizePhysicalPosition(), CMiniportWaveCyclicStream::SetBufferSize() (+4 more)

### Community 117 - "ma_resampler_config"
Cohesion: 0.12
Nodes (30): ma_channel_converter_config_init_from_data_converter_config(), ma_convert_frames_ex(), ma_data_converter_config, ma_data_converter_config_get_mid_format(), ma_data_converter_config_is_resampler_required(), ma_data_converter_get_expected_output_frame_count(), ma_data_converter_get_heap_layout(), ma_data_converter_get_heap_size() (+22 more)

### Community 118 - "ma_audio_buffer"
Cohesion: 0.10
Nodes (31): ma_audio_buffer, ma_audio_buffer_alloc_and_init(), ma_audio_buffer_at_end(), ma_audio_buffer_config, ma_audio_buffer_get_available_frames(), ma_audio_buffer_get_cursor_in_pcm_frames(), ma_audio_buffer_get_length_in_pcm_frames(), ma_audio_buffer_init() (+23 more)

### Community 119 - "ma_encoder"
Cohesion: 0.11
Nodes (24): ma_encoder, config, data, ma_encoder_init(), ma_encoder_init_file(), ma_encoder_init_file_w(), ma_encoder_init__internal(), ma_encoder_init_vfs() (+16 more)

### Community 120 - "ma_linear_resampler"
Cohesion: 0.12
Nodes (29): ma_linear_resampler, ma_linear_resampler_adjust_timer_for_new_rate(), ma_linear_resampler_config, ma_linear_resampler_get_expected_output_frame_count(), ma_linear_resampler_get_heap_layout(), ma_linear_resampler_get_heap_size(), ma_linear_resampler_get_input_latency(), ma_linear_resampler_get_output_latency() (+21 more)

### Community 121 - "ma_resource_manager_data_stream"
Cohesion: 0.15
Nodes (30): ma_data_source_get_loop_point_in_pcm_frames(), ma_data_source_get_range_in_pcm_frames(), ma_data_source_set_loop_point_in_pcm_frames(), ma_job_init(), ma_job_process__resource_manager__load_data_stream(), ma_job_process__resource_manager__page_data_stream(), ma_job_process__resource_manager__seek_data_stream(), ma_resource_manager_data_stream (+22 more)

### Community 122 - "ma_data_source_get_data_format"
Cohesion: 0.10
Nodes (29): ma_calculate_frame_count_after_resampling(), ma_data_source_get_cursor_in_pcm_frames(), ma_data_source_get_cursor_in_seconds(), ma_data_source_get_data_format(), ma_data_source_get_length_in_pcm_frames(), ma_data_source_get_length_in_seconds(), ma_data_source_node_process_pcm_frames(), ma_data_source_read_pcm_frames() (+21 more)

### Community 123 - "ma_job_queue_next"
Cohesion: 0.14
Nodes (19): ma_device_job_thread_next(), ma_device_job_thread_post(), ma_device_job_thread_uninit(), ma_ffs_32(), ma_job_extract_slot(), ma_job_queue_get_heap_layout(), ma_job_queue_get_heap_size(), ma_job_queue_init_preallocated() (+11 more)

### Community 124 - "main"
Cohesion: 0.19
Nodes (23): ex_load_pem(), ex_read_file(), load_cert(), main(), cert_message(), check_peer(), name_ok(), vban_ca_cert() (+15 more)

### Community 125 - "ma_biquad_init_preallocated"
Cohesion: 0.14
Nodes (23): ma_biquad, ma_biquad_config, ma_biquad_get_heap_layout(), ma_biquad_get_heap_size(), ma_biquad_init(), ma_biquad_init_preallocated(), ma_biquad_node_process_pcm_frames(), ma_biquad_node_reinit() (+15 more)

### Community 126 - "ma_bpf_reinit__internal"
Cohesion: 0.15
Nodes (22): ma_bpf, ma_bpf2, ma_bpf2_config, ma_bpf2_get_heap_size(), ma_bpf2_init(), ma_bpf2_init_preallocated(), ma_bpf2_process_pcm_frame_f32(), ma_bpf2_process_pcm_frames() (+14 more)

### Community 127 - "ma_channel_converter_init_preallocated"
Cohesion: 0.15
Nodes (24): ma_calculate_channel_position_rectangular_weight(), ma_channel_conversion_path, ma_channel_converter_config, ma_channel_converter_config_get_conversion_path(), ma_channel_converter_config ma_channel_converter_config_init(), ma_channel_converter_float_to_fixed(), ma_channel_converter_get_heap_layout(), ma_channel_converter_get_heap_size() (+16 more)

### Community 128 - "ma_job"
Cohesion: 0.17
Nodes (22): ma_async_notification_poll__on_signal(), ma_async_notification_signal(), ma_fence_release(), ma_job, data, order, ma_job_process(), ma_job_process__custom() (+14 more)

### Community 129 - "ma_noise"
Cohesion: 0.15
Nodes (19): ma_noise__data_source_on_read(), ma_noise_f32_brownian(), ma_noise_f32_pink(), ma_noise_f32_white(), ma_noise_get_heap_layout(), ma_noise_get_heap_size(), ma_noise_init(), ma_noise_init_preallocated() (+11 more)

### Community 130 - "ma_completion_handler_uwp"
Cohesion: 0.10
Nodes (17): ma_completion_handler_uwp, hEvent, lpVtbl, ma_completion_handler_uwp_uninit(), ma_completion_handler_uwp_wait(), ma_IActivateAudioInterfaceAsyncOperation, ma_IActivateAudioInterfaceAsyncOperation_AddRef(), lpVtbl (+9 more)

### Community 131 - "ma_dr_flac_oggbs__goto_next_page"
Cohesion: 0.16
Nodes (16): ma_dr_flac_ogg__get_page_body_size(), ma_dr_flac_ogg__get_page_header_size(), ma_dr_flac_ogg__is_capture_pattern(), ma_dr_flac_ogg__read_page_header(), ma_dr_flac_ogg__read_page_header_after_capture_pattern(), ma_dr_flac_oggbs__get_current_segment_index(), ma_dr_flac_oggbs__goto_next_page(), ma_dr_flac_oggbs__read_physical() (+8 more)

### Community 132 - "ma_IDirectSoundCaptureBuffer"
Cohesion: 0.11
Nodes (15): ma_IDirectSoundCapture, ma_IDirectSoundCapture_AddRef(), ma_IDirectSoundCapture_CreateCaptureBuffer(), ma_IDirectSoundCapture_GetCaps(), ma_IDirectSoundCapture_Initialize(), lpVtbl, ma_IDirectSoundCapture_QueryInterface(), ma_IDirectSoundCaptureBuffer (+7 more)

### Community 133 - "ma_dr_mp3__full_read_and_close_s16"
Cohesion: 0.23
Nodes (16): ma_dr_mp3_copy_allocation_callbacks_or_defaults(), ma_dr_mp3__free_from_callbacks(), ma_dr_mp3__full_read_and_close_f32(), ma_dr_mp3__full_read_and_close_s16(), ma_dr_mp3_init(), ma_dr_mp3_init_file(), ma_dr_mp3_init_internal(), ma_dr_mp3_init_memory() (+8 more)

### Community 134 - "ma_resource_manager_data_buffer_init_ex_internal"
Cohesion: 0.23
Nodes (17): ma_data_source_config, ma_data_source_init(), ma_resource_manager_data_buffer_init_ex_internal(), ma_resource_manager_data_buffer_next_execution_order(), ma_resource_manager_data_buffer_result(), ma_resource_manager_data_buffer_uninit(), ma_resource_manager_data_stream_init(), ma_resource_manager_data_stream_init_ex() (+9 more)

### Community 135 - "scream.h"
Cohesion: 0.19
Nodes (6): AddDevice(), DriverEntry(), GetRegistrySettings(), InstallSubdevice(), StartDevice(), NewAdapterCommon()

### Community 136 - "ma_get_bytes_per_sample"
Cohesion: 0.21
Nodes (10): ma_allocate_AudioBufferList__coreaudio(), ma_deinterleave_pcm_frames(), ma_device__jack_buffer_size_callback(), ma_device__jack_process_callback(), ma_device_realloc_AudioBufferList__coreaudio(), ma_get_bytes_per_sample(), ma_interleave_pcm_frames(), ma_on_input__coreaudio() (+2 more)

### Community 137 - "vban_sec_session"
Cohesion: 0.13
Nodes (15): vban_sec_session, hs, hs_ms, initiator, local_idx, peer, remote_idx, rx_any (+7 more)

### Community 138 - "ma_sio_par"
Cohesion: 0.13
Nodes (15): ma_sio_par, appbufsz, bits, bps, bufsz, le, __magic, msb (+7 more)

### Community 139 - "vban_noise_hs"
Cohesion: 0.14
Nodes (14): vban_noise_cipher_t, vban_noise_hs, ck, cs, e_pk, e_sk, failed, h (+6 more)

### Community 140 - "wchar_t"
Cohesion: 0.31
Nodes (14): ma_copy_string_w(), ma_decoder_init_custom_from_file_w__internal(), ma_decoder_init_file_w(), ma_decoder_init_flac_from_file_w__internal(), ma_decoder_init_from_file_w__internal(), ma_decoder_init_mp3_from_file_w__internal(), ma_decoder_init_vorbis_from_file_w__internal(), ma_decoder_init_wav_from_file_w__internal() (+6 more)

### Community 141 - "ma_panner"
Cohesion: 0.16
Nodes (14): ma_pan_mode, ma_pan_mode ma_panner_get_mode(), ma_panner, ma_panner_config, ma_panner_get_pan(), ma_panner_init(), ma_panner_set_mode(), ma_panner_set_pan() (+6 more)

### Community 142 - "ma_IDirectSound"
Cohesion: 0.17
Nodes (10): ma_IDirectSound, ma_IDirectSound_AddRef(), ma_IDirectSound_Compact(), ma_IDirectSound_CreateSoundBuffer(), ma_IDirectSound_DuplicateSoundBuffer(), lpVtbl, ma_IDirectSound_QueryInterface(), ma_IDirectSound_SetCooperativeLevel() (+2 more)

### Community 143 - "IID"
Cohesion: 0.15
Nodes (11): ma_IAudioClient2_GetService(), ma_IAudioClient2_QueryInterface(), ma_IAudioClient3_GetService(), ma_IAudioClient3_QueryInterface(), ma_IAudioRenderClient, ma_IAudioRenderClient_AddRef(), lpVtbl, ma_IAudioRenderClient_QueryInterface() (+3 more)

### Community 144 - "vban_cert.c"
Cohesion: 0.23
Nodes (8): hex(), parse_role(), print_cert(), print_time(), role_text(), save(), write_new(), vban_pem_encode()

### Community 145 - "ma_semaphore"
Cohesion: 0.27
Nodes (12): ma_semaphore, ma_semaphore_init(), ma_semaphore_init__win32(), ma_semaphore_release(), ma_semaphore_release__posix(), ma_semaphore_release__win32(), ma_semaphore_uninit(), ma_semaphore_uninit__posix() (+4 more)

### Community 146 - "ma_audio_worklet_processor_created__webaudio"
Cohesion: 0.20
Nodes (7): ma_audio_worklet_process_callback__webaudio(), ma_audio_worklet_processor_created__webaudio(), ma_audio_worklet_thread_initialized__webaudio(), ma_log10d(), ma_log10f(), ma_logd(), log

### Community 147 - "tagWAVEHDR"
Cohesion: 0.20
Nodes (9): tagWAVEHDR, dwBufferLength, dwBytesRecorded, dwFlags, dwLoops, dwUser, lpData, lpNext (+1 more)

### Community 148 - "ma_IPropertyStore"
Cohesion: 0.22
Nodes (8): ma_IPropertyStore, ma_IPropertyStore_AddRef(), ma_IPropertyStore_Commit(), ma_IPropertyStore_GetAt(), ma_IPropertyStore_GetCount(), lpVtbl, ma_IPropertyStore_QueryInterface(), ma_IPropertyStore_SetValue()

### Community 149 - "SottoVBAN/common.h"
Cohesion: 0.22
Nodes (3): CAdapterCommon::NonDelegatingQueryInterface(), DECLARE_INTERFACE_(), PADAPTERCOMMON

### Community 150 - "ma_SLDataFormat_PCM_init__opensl"
Cohesion: 0.33
Nodes (7): ma_channel_id_to_ma__opensl(), ma_channel_id_to_opensl(), ma_channel_map_to_channel_mask__opensl(), ma_channel_mask_to_channel_map__opensl(), ma_deconstruct_SLDataFormat_PCM__opensl(), ma_round_to_standard_sample_rate__opensl(), ma_SLDataFormat_PCM_init__opensl()

### Community 151 - "ma_thread_wait"
Cohesion: 0.36
Nodes (8): ma_thread, ma_thread_create__posix(), ma_thread_create__win32(), ma_thread_priority, ma_thread_priority_to_win32(), ma_thread_wait(), ma_thread_wait__posix(), ma_thread_wait__win32()

### Community 152 - "ma_IDirectSoundNotify"
Cohesion: 0.25
Nodes (6): ma_IDirectSoundNotify, ma_IDirectSoundNotify_AddRef(), lpVtbl, ma_IDirectSoundNotify_QueryInterface(), ma_IDirectSoundNotify_Release(), ma_IDirectSoundNotify_SetNotificationPositions()

### Community 153 - "vban_sec_id"
Cohesion: 0.25
Nodes (8): vban_sec_id, authorize_ctx, ca_pk, cert, cert_bytes, now_unix, pk, sk

### Community 154 - "ma_sio_cap"
Cohesion: 0.25
Nodes (8): ma_sio_cap, confs, enc, nconf, __pad, pchan, rate, rchan

### Community 155 - "SottoAudio: virtual sound devices for macOS"
Cohesion: 0.33
Nodes (5): Build and install, SottoAudio: virtual sound devices for macOS, Status, Uninstall, Use it

### Community 156 - "Sotto VBAN: virtual sound card for Windows"
Cohesion: 0.33
Nodes (5): Build, Install (test machine), Receiving on Windows, Sotto VBAN: virtual sound card for Windows, Status

### Community 157 - "ma_data_source_set_looping"
Cohesion: 0.40
Nodes (6): ma_data_source_node_set_looping(), ma_data_source_set_looping(), ma_resource_manager_data_buffer_cb__set_looping(), ma_resource_manager_data_buffer_set_looping(), ma_resource_manager_data_source_set_looping(), ma_resource_manager_data_stream_set_looping()

### Community 158 - "container-test.sh"
Cohesion: 0.50
Nodes (4): check(), DBUS_SESSION_BUS_ADDRESS, container-test.sh script, XDG_RUNTIME_DIR

### Community 160 - "ma_sio_conf"
Cohesion: 0.40
Nodes (5): ma_sio_conf, enc, pchan, rate, rchan

### Community 161 - "ma_paged_audio_buffer_data_allocate_page"
Cohesion: 0.50
Nodes (4): ma_paged_audio_buffer_data_allocate_page(), ma_paged_audio_buffer_page, pAudioData, sizeInFrames

### Community 164 - "ma_pthread_cond_t"
Cohesion: 0.67
Nodes (3): ma_pthread_cond_t, __alignment, __data

### Community 165 - "ma_pthread_mutex_t"
Cohesion: 0.67
Nodes (3): ma_pthread_mutex_t, __alignment, __data

## Knowledge Gaps
- **682 isolated node(s):** `XDG_RUNTIME_DIR`, `DBUS_SESSION_BUS_ADDRESS`, `m_pPortWave`, `m_pServiceGroupWave`, `m_pDeviceObject` (+677 more)
  These have ≤1 connection - possible missing edges or undocumented components. (Counts symbols only; 1190 node(s) total have ≤1 connection when file, concept and rationale nodes are included.)
- **25 thin communities (<3 nodes) omitted from report** — run `graphify query` to explore isolated nodes.

## Suggested Questions
_Questions this graph is uniquely positioned to answer:_

- **Why does `vban_jb` connect `Jitter Buffer State` to `vban_jitter.c`, `vban_proto.h`?**
  _High betweenness centrality (0.028) - this node is a cross-community bridge._
- **What connects `XDG_RUNTIME_DIR`, `DBUS_SESSION_BUS_ADDRESS`, `m_pPortWave` to the rest of the system?**
  _682 weakly-connected nodes found - possible documentation gaps or missing edges._
- **Should `vban_jitter.c` be split into smaller, more focused modules?**
  _Cohesion score 0.08897243107769423 - nodes in this community are weakly interconnected._
- **Why does `CMiniportWaveCyclicStream` connect `CMiniportWaveCyclicStream` to `CSaveData`, `CIVSHMEMSaveData`, `ivshmemsavedata.h`, `minstream.cpp`?**
  _High betweenness centrality (0.027) - this node is a cross-community bridge._
- **Should `Jitter Buffer State` be split into smaller, more focused modules?**
  _Cohesion score 0.03571428571428571 - nodes in this community are weakly interconnected._
- **Why does `ma_device` connect `ma_result` to `vban_jitter.c`, `ma_get_bytes_per_sample`, `miniaudio.h`, `main`, `MA_API`, `ma_thread_wait`, `ma_sound`, `ma_log_postf`, `ma_device_config`, `ma_device_init`, `ma_allocation_callbacks`, `ma_uint32`, `ma_context`, `ma_strncpy_s`, `ma_get_bytes_per_frame`, `ma_pcm_rb`, `type`, `ma_device_info`, `DWORD`?**
  _High betweenness centrality (0.024) - this node is a cross-community bridge._
- **Should `Mixer & C++ Wrapper` be split into smaller, more focused modules?**
  _Cohesion score 0.05174825174825175 - nodes in this community are weakly interconnected._