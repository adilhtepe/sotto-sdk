/* Kernel build: the SDK's portable headers include <stdint.h>; map it. */
#ifndef VBAN_KSHIM_STDINT_H
#define VBAN_KSHIM_STDINT_H
#include <linux/types.h>
#include <linux/limits.h>
#ifndef INT32_MAX
#define INT32_MAX S32_MAX
#define INT32_MIN S32_MIN
#endif
#endif
