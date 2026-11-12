/*
 * VBAN SDK - umbrella header.
 *
 *   vban_proto  : wire format, parsing, builders, PING0, sequencing  (pure C, no OS)
 *   vban_pcm    : sample format conversion, gain, peak               (pure C, no OS)
 *   vban_jitter : lock-free receive jitter buffer, PLC, drift        (pure C, no OS)
 *   vban_mixer  : N x M matrix mixer with ramps and limiter          (pure C, no OS)
 *   vban_rx     : demux, PING0 responder, reassembly, RT-Packet       (pure C, no OS)
 *   vban_tx     : zero-copy packetizer, text/serial/MIDI/frame/ping   (pure C + clock)
 *   vban_net    : UDP sockets, batching, clock, RT thread priority     (POSIX / Win32)
 */
#ifndef VBAN_H
#define VBAN_H

#define VBAN_SDK_VERSION_MAJOR 1
#define VBAN_SDK_VERSION_MINOR 0
#define VBAN_SDK_VERSION_PATCH 0
#define VBAN_SDK_VERSION "1.0.0"

#include "vban/vban_config.h"
#include "vban/vban_proto.h"
#include "vban/vban_pcm.h"
#include "vban/vban_jitter.h"
#include "vban/vban_mixer.h"
#include "vban/vban_rx.h"
#include "vban/vban_tx.h"
#include "vban/vban_net.h"

#endif /* VBAN_H */
