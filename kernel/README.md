# snd-vban: VBAN sound card for Linux

`snd-vban` is an ALSA virtual sound card driver. Playback is sent as a VBAN stream, and capture
plays one received VBAN stream. Any Linux application (ALSA, PipeWire, JACK, `aplay`/`arecord`)
can then talk to Voicemeeter, the OpenWrt hub or SDK devices, with no userspace daemon.

```bash
make                                  # against the running kernel's headers
```
```bash
sudo insmod snd-vban.ko dest=192.168.2.1:6980 tx_name=Linux listen_port=6980 rx_name=Mix
```
```bash
aplay -D hw:VBAN,0 song.wav           # -> VBAN stream "Linux" to 192.168.2.1
```
```bash
arecord -D hw:VBAN,0 -f S16_LE -r 48000 -c 2 rec.wav   # <- VBAN stream "Mix"
```
```bash
cat /proc/asound/VBAN/vban            # packets, lost, repaired, late, dup, concealment, buffer
```

| Parameter | Default | Meaning |
|---|---|---|
| `dest` | `127.0.0.1:6980` | Playback destination `ip:port` (broadcast allowed; `""` disables) |
| `tx_name` | `Linux` | Stream name sent |
| `listen_port` | `6980` | UDP port for the capture stream (`0` disables) |
| `rx_name` | `Stream1` | Stream name accepted (exact, case-sensitive) |
| `rx_source` | `""` | Only accept capture from this IPv4 |
| `rate` | `48000` | Sample rate; must be a VBAN rate |
| `channels` | `2` | 1..8 |
| `fpp` | `64` | Frames per packet, 16..256 (64 @ 48 kHz = 1.33 ms) |
| `rx_target` | `256` | Capture buffering target in frames (5.3 ms @ 48 kHz) |

Format: S16_LE.

## How it works
- **Clock.** A `SCHED_FIFO` kernel thread wakes every packet period on an hrtimer.
  - Playback: it takes `fpp` frames from the ALSA buffer, wraps them in a VBAN header and sends them
    (`kernel_sendmsg`, DSCP EF).
  - Capture: it fills `fpp` frames from the receive ring.
  - Both: it advances the hardware pointers and calls `snd_pcm_period_elapsed()`.
- **Receive.** A second kernel thread reads the UDP socket, validates each packet with the SDK's
  `vban_parse()` and places it in a ring by frame counter. The SDK's hardened `vban_seq` decides
  loss, late arrival, restart and spoofed jumps. Reordered packets and redundant copies
  (`vban_tx_set_redundancy`) fill their slots; duplicates are dropped.
- **Missing audio** is concealed at playout time with an integer decay, then a fade-in. A hole with
  newer audio after it keeps the clock running. Starvation holds playout. Sender clock drift is
  followed by an integer (Q16) linear-interpolation resampler, within ±0.5 %, with the SDK's control
  law; a large backlog is skipped in one jump.
- **Capture idle.** While nobody captures, packets are counted but not buffered. Each capture start
  re-anchors to the live stream, so it never starts from stale audio.
- **Shared code.** `src/vban_proto.c` from the SDK is compiled into the module unchanged, through
  `shim/` (`<stdint.h>` etc. mapped to kernel headers). The receive ring is `vban_kring.h`, which
  also builds in userspace, so the SDK's tests run the module's exact code. All kernel code is
  integer-only (no FPU).
- **Locking.**
  - `pcm_lock` (irqsave) guards trigger and pointer state.
  - `rx_lock` guards the ring.
  - The clock thread holds `work_mutex` while it touches a substream, and `.sync_stop` takes it.
    So `hw_free`/`close` never race an in-flight copy or `period_elapsed()`.
  - `sendmsg` (which can sleep) is only called with no spinlock held.

## Tests
**Parity with the SDK** (`vban_parity` in the SDK's `ctest`). `vban_kring.h` and the SDK jitter
buffer both run through `vban_netsim`'s networks (40 s each, without and with 4-deep copies), with
the same buffering target. The kernel must stay within 15 % (+20 ms/min) of the SDK's glitch time,
buffer at most 1.5 ms more, and never re-prime. Wired LAN must be glitch-free in both.

| Scenario | Glitch SDK / kernel (ms/min) | Buffered SDK / kernel (ms) | With `dup:4` (ms/min) |
|---|---|---|---|
| Wired LAN | 0 / 0 | 6.71 / 6.71 | 0 / 0 |
| Wi-Fi good | 316 / 312 | 15.8 / 16.3 | 32 / 28 |
| Wi-Fi congested | 1676 / 1683 | 40.6 / 39.3 | 348 / 360 |
| WAN / VPN | 550 / 517 | 33.3 / 34.7 | 78 / 50 |

**In a VM.** `vm-test.sh` boots this machine's own kernel in a throwaway QEMU VM with virtme-ng. The module is
never loaded into the host:
```bash
sudo vng -r /boot/vmlinuz-$(uname -r) --memory 2G --exec "sh $PWD/vm-test.sh"
```
It checks:
- **Playback:** `aplay` → snd-vban → VBAN → SDK `vban_recv`.
- **Capture:** SDK `vban_send` → VBAN → snd-vban → `arecord`.
- **Capture with copies:** the same, from a sender with redundant copies (`-R 4`).
- **Stress:** 30 open/close cycles of both directions while streaming.
- **Unload:** `rmmod`, then a scan of the kernel log.

Result on 7.0.0-43-generic (x86-64):

| Check | Result |
|---|---|
| Playback | 440 Hz tone arrives with peak 8192; largest sample step 473 (472 for a clean tone) |
| Capture | 2.00 s, peak 8192, step 472 (resampler adds no distortion), 0 lost, 0 concealed |
| Capture with copies | 1497 duplicates dropped, step 472, 0 concealed |
| Stress, then `rmmod` | no oops or warnings; `rmmod` clean |

The `W=1` build is clean.

**Not covered.** The host kernel has no lockdep / `DEBUG_ATOMIC_SLEEP`, so locking was checked by
design and tests, not by those tools. Only x86-64 was built and run. There is no PING0 responder
in the module. The only format is S16_LE.

## Bugs found by the parity test (fixed)
- **Stalled-hole lockup.** After a starvation, a hole with newer audio behind it kept the ring
  stalled until a re-prime (14 re-primes, 2983 vs 1799 ms/min glitch on "Wi-Fi congested").
- **Slip storm.** Drift correction by one-frame slips reacted to network jitter (36 000 slips in
  120 s). Slowed down, slips either kept a stall's extra latency for seconds (+1.5 ms vs the SDK) or
  clicked 54 times/s. They were replaced by the resampler: no slips, latency within 1.4 ms of the SDK.

## Bugs found by the VM tests (fixed)
- **Stale capture timeline.** Capture opened after the stream had been running a while played
  ~0.2 s of concealment, and then slipped continuously. While idle the ring filled, and the timeline
  went stale. It is fixed by the "capture idle" behaviour and a skip for large backlogs.
- **SDK example tools** only stopped cleanly on SIGINT. Under `timeout`/systemd (SIGTERM),
  `vban_recv` died without flushing its output. They now handle SIGTERM too.
