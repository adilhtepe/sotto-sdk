/*
 * VBAN SDK - minimal portable acquire/release atomics for SPSC rings.
 * Only 32-bit loads and stores are needed (one writer per variable).
 *
 * Backends, in order of preference:
 *   - GCC/Clang __atomic builtins, when 32-bit access is lock-free
 *   - AVR: interrupts masked around the access (a 32-bit load/store is four
 *     instructions there and could be torn by an ISR; avr-libc has no
 *     __atomic_load_4 either)
 *   - MSVC intrinsics
 *   - other GCC/Clang targets without lock-free 32-bit atomics (e.g. RISC-V
 *     without the A extension): volatile access + compiler barrier, which is
 *     correct on the single-core parts such cores ship in
 *   - anything else: plain volatile (single-core MCUs)
 */
#ifndef VBAN_ATOMIC_H
#define VBAN_ATOMIC_H

#include <stdint.h>

typedef volatile uint32_t vban_atomic_u32;

#if (defined(__GNUC__) || defined(__clang__)) && \
    ((defined(__SIZEOF_INT__) && __SIZEOF_INT__ == 4 && __GCC_ATOMIC_INT_LOCK_FREE == 2) || \
     (defined(__SIZEOF_LONG__) && __SIZEOF_LONG__ == 4 && __GCC_ATOMIC_LONG_LOCK_FREE == 2))
static inline uint32_t vban_load_acq(const vban_atomic_u32 *p) { return __atomic_load_n(p, __ATOMIC_ACQUIRE); }
static inline void vban_store_rel(vban_atomic_u32 *p, uint32_t v) { __atomic_store_n(p, v, __ATOMIC_RELEASE); }
static inline uint32_t vban_load_rlx(const vban_atomic_u32 *p) { return __atomic_load_n(p, __ATOMIC_RELAXED); }
static inline void vban_store_rlx(vban_atomic_u32 *p, uint32_t v) { __atomic_store_n(p, v, __ATOMIC_RELAXED); }
#elif defined(__AVR__)
#include <util/atomic.h>
/* ATOMIC_BLOCK saves SREG, masks interrupts and restores SREG; it also acts
 * as a compiler memory barrier, which gives acquire/release on one core. */
static inline uint32_t vban_load_acq(const vban_atomic_u32 *p)
{
    uint32_t v = 0;
    ATOMIC_BLOCK(ATOMIC_RESTORESTATE) { v = *p; }
    return v;
}
static inline void vban_store_rel(vban_atomic_u32 *p, uint32_t v) { ATOMIC_BLOCK(ATOMIC_RESTORESTATE) { *p = v; } }
static inline uint32_t vban_load_rlx(const vban_atomic_u32 *p) { return vban_load_acq(p); }
static inline void vban_store_rlx(vban_atomic_u32 *p, uint32_t v) { vban_store_rel(p, v); }
#elif defined(_MSC_VER)
#include <intrin.h>
#if defined(_M_IX86) || defined(_M_X64)
/* x86 is TSO: a compiler barrier gives acquire/release. */
static __forceinline uint32_t vban_load_acq(const vban_atomic_u32 *p) { uint32_t v = *p; _ReadWriteBarrier(); return v; }
static __forceinline void vban_store_rel(vban_atomic_u32 *p, uint32_t v) { _ReadWriteBarrier(); *p = v; }
#else
static __forceinline uint32_t vban_load_acq(const vban_atomic_u32 *p) { return (uint32_t)_InterlockedOr((volatile long *)p, 0); }
static __forceinline void vban_store_rel(vban_atomic_u32 *p, uint32_t v) { _InterlockedExchange((volatile long *)p, (long)v); }
#endif
static __forceinline uint32_t vban_load_rlx(const vban_atomic_u32 *p) { return *p; }
static __forceinline void vban_store_rlx(vban_atomic_u32 *p, uint32_t v) { *p = v; }
#elif defined(__GNUC__) || defined(__clang__)
/* Single-core: aligned 32-bit access is one instruction on 32-bit cores; the
 * barrier stops the compiler moving ring reads/writes across the index. */
#define VBAN_COMPILER_BARRIER() __asm__ __volatile__("" ::: "memory")
static inline uint32_t vban_load_acq(const vban_atomic_u32 *p) { uint32_t v = *p; VBAN_COMPILER_BARRIER(); return v; }
static inline void vban_store_rel(vban_atomic_u32 *p, uint32_t v) { VBAN_COMPILER_BARRIER(); *p = v; }
static inline uint32_t vban_load_rlx(const vban_atomic_u32 *p) { return *p; }
static inline void vban_store_rlx(vban_atomic_u32 *p, uint32_t v) { *p = v; }
#else
/* Single-core MCUs / unknown compilers: volatile is sufficient there. */
static inline uint32_t vban_load_acq(const vban_atomic_u32 *p) { return *p; }
static inline void vban_store_rel(vban_atomic_u32 *p, uint32_t v) { *p = v; }
static inline uint32_t vban_load_rlx(const vban_atomic_u32 *p) { return *p; }
static inline void vban_store_rlx(vban_atomic_u32 *p, uint32_t v) { *p = v; }
#endif

#endif /* VBAN_ATOMIC_H */
