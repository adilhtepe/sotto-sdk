/* Shared helpers for the example tools (tiny getopt-free argument parsing). */
#ifndef VBAN_EXAMPLES_COMMON_H
#define VBAN_EXAMPLES_COMMON_H

#include "vban.h"
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#ifdef _WIN32
#include <fcntl.h>
#include <io.h>
#endif

static volatile sig_atomic_t g_stop;
static void on_sigint(int s) { (void)s; g_stop = 1; }

static void ex_init(void)
{
    signal(SIGINT, on_sigint);
    signal(SIGTERM, on_sigint);  /* systemd / timeout: stop cleanly and flush output */
#ifdef _WIN32
    _setmode(_fileno(stdout), _O_BINARY);
    _setmode(_fileno(stdin), _O_BINARY);
#endif
    if (vban_net_init() != VBAN_OK) { fprintf(stderr, "network init failed\n"); exit(1); }
}

/* "-x value" lookup */
static inline const char *arg_str(int argc, char **argv, const char *key, const char *def)
{
    int i;
    for (i = 1; i + 1 < argc; ++i) if (strcmp(argv[i], key) == 0) return argv[i + 1];
    return def;
}
static inline long arg_int(int argc, char **argv, const char *key, long def)
{
    const char *s = arg_str(argc, argv, key, NULL);
    return s ? strtol(s, NULL, 0) : def;
}
static inline int arg_flag(int argc, char **argv, const char *key)
{
    int i;
    for (i = 1; i < argc; ++i) if (strcmp(argv[i], key) == 0) return 1;
    return 0;
}

static inline int parse_format(const char *s)
{
    if (!strcmp(s, "u8")) return VBAN_DATATYPE_U8;
    if (!strcmp(s, "s16")) return VBAN_DATATYPE_S16;
    if (!strcmp(s, "s24")) return VBAN_DATATYPE_S24;
    if (!strcmp(s, "s32")) return VBAN_DATATYPE_S32;
    if (!strcmp(s, "f32")) return VBAN_DATATYPE_F32;
    if (!strcmp(s, "f64")) return VBAN_DATATYPE_F64;
    fprintf(stderr, "unknown format '%s' (u8 s16 s24 s32 f32 f64)\n", s);
    exit(2);
}

static inline const char *format_name(unsigned dt)
{
    static const char *n[8] = { "u8", "s16", "s24", "s32", "f32", "f64", "s12", "s10" };
    return n[dt & 7];
}

static inline const char *protocol_name(unsigned p)
{
    switch (p) {
    case VBAN_PROTOCOL_AUDIO: return "AUDIO";
    case VBAN_PROTOCOL_SERIAL: return "SERIAL";
    case VBAN_PROTOCOL_TXT: return "TEXT";
    case VBAN_PROTOCOL_SERVICE: return "SERVICE";
    case VBAN_PROTOCOL_FRAME: return "FRAME";
    default: return "USER/UNDEF";
    }
}

/* Make network-supplied text safe for a terminal (SEC-008): C0/C1 control
 * characters (ESC sequences, 8-bit CSI 0x9B, ...) and malformed UTF-8 become
 * '?'. Valid printable UTF-8 is kept. Writes at most cap-1 bytes + NUL. */
static inline char *ex_safe_text(const char *in, size_t n, char *out, size_t cap)
{
    const unsigned char *s = (const unsigned char *)in;
    size_t i = 0, o = 0;
    if (cap == 0) return out;
    while (i < n && s[i] && o + 1 < cap) {
        unsigned c = s[i], len = c < 0x80 ? 1 : (c >> 5) == 6 ? 2 : (c >> 4) == 14 ? 3 : (c >> 3) == 30 ? 4 : 0;
        unsigned cp = len == 1 ? c : len == 2 ? (c & 31) : len == 3 ? (c & 15) : (c & 7), k;
        int ok = len != 0 && i + len <= n && o + len < cap;
        for (k = 1; ok && k < len; ++k) {
            if ((s[i + k] & 0xC0) != 0x80) ok = 0;
            else cp = (cp << 6) | (s[i + k] & 63u);
        }
        if (ok && (cp < 0x20 || cp == 0x7F || (cp >= 0x80 && cp < 0xA0))) ok = 0;
        if (ok) { for (k = 0; k < len; ++k) out[o++] = (char)s[i + k]; i += len; }
        else { out[o++] = '?'; i += len ? len : 1; }
    }
    out[o] = 0;
    return out;
}

/* ---- secure mode: --ca FILE --cert FILE --key FILE (text files from vban_cert) ---- */
static inline int ex_read_file(const char *path, char *buf, size_t cap)
{
    FILE *f = fopen(path, "rb");
    size_t n;
    if (!f) return -1;
    n = fread(buf, 1, cap - 1, f);
    fclose(f);
    buf[n] = 0;
    return (int)n;
}

static inline int ex_load_pem(const char *path, const char *label, uint8_t *out, size_t len)
{
    char txt[4096];
    int n = ex_read_file(path, txt, sizeof txt);
    if (n < 0) { fprintf(stderr, "cannot read %s\n", path); return -1; }
    if (vban_pem_decode(label, txt, (size_t)n, out, len) != (int)len) {
        fprintf(stderr, "%s: no valid %s\n", path, label);
        return -1;
    }
    memset(txt, 0, sizeof txt);
    return 0;
}

/* Returns 1 if secure mode was requested and loaded, 0 if not requested, -1 on error. */
static inline int ex_load_identity(int argc, char **argv, vban_sec_id_t *id)
{
    const char *ca = arg_str(argc, argv, "--ca", NULL), *cert = arg_str(argc, argv, "--cert", NULL),
               *key = arg_str(argc, argv, "--key", NULL);
    uint8_t cab[VBAN_CERT_SIZE], cb[VBAN_CERT_SIZE], sk[32];
    int r;
    if (!ca && !cert && !key) return 0;
    if (!ca || !cert || !key) { fprintf(stderr, "secure mode needs --ca, --cert and --key\n"); return -1; }
    if (ex_load_pem(ca, "VBAN CERT", cab, sizeof cab) || ex_load_pem(cert, "VBAN CERT", cb, sizeof cb) ||
        ex_load_pem(key, "VBAN PRIVATE KEY", sk, sizeof sk))
        return -1;
    r = vban_sec_id_init(id, sk, cb, sizeof cb, cab, sizeof cab);
    memset(sk, 0, sizeof sk);
    if (r) { fprintf(stderr, "identity rejected: %s\n", vban_strerror(r)); return -1; }
    id->now_unix = (uint64_t)time(NULL);
    return 1;
}

#endif
