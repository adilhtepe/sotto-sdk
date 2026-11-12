/* VBAN SDK - unit and integration tests (no external framework). */
#include "vban.h"
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int g_fail, g_checks;
#define CHECK(c) do { ++g_checks; if (!(c)) { ++g_fail; printf("  FAIL %s:%d: %s\n", __FILE__, __LINE__, #c); } } while (0)
#define CHECK_EQ(a, b) do { long long _a = (long long)(a), _b = (long long)(b); ++g_checks; \
    if (_a != _b) { ++g_fail; printf("  FAIL %s:%d: %s == %s (%lld vs %lld)\n", __FILE__, __LINE__, #a, #b, _a, _b); } } while (0)
#define CHECK_NEAR(a, b, eps) do { double _a = (a), _b = (b); ++g_checks; \
    if (fabs(_a - _b) > (eps)) { ++g_fail; printf("  FAIL %s:%d: %s ~ %s (%g vs %g)\n", __FILE__, __LINE__, #a, #b, _a, _b); } } while (0)
#define RUN(t) do { printf("[%s]\n", #t); t(); } while (0)

/* --------------------------------------------------- capture sink */
#define CAP_MAX 512
typedef struct { uint8_t pkt[CAP_MAX][VBAN_PACKET_MAX_SIZE]; size_t len[CAP_MAX]; vban_addr_t to[CAP_MAX]; int n; } capture_t;
static int cap_send(void *ctx, const uint8_t *p, size_t len, const vban_addr_t *to)
{
    capture_t *c = (capture_t *)ctx;
    if (c->n >= CAP_MAX || len > VBAN_PACKET_MAX_SIZE) return -1;
    memcpy(c->pkt[c->n], p, len);
    c->len[c->n] = len;
    c->to[c->n] = *to;
    c->n++;
    return (int)len;
}

/* =================================================== protocol */

static void test_header_and_spec_examples(void)
{
    uint8_t b[VBAN_PACKET_MAX_SIZE];
    vban_name_t n;
    vban_header_t h, h2;
    int len;

    vban_name_set(&n, "Command1");
    len = vban_build_text(b, sizeof b, &n, 7, 18, 0, VBAN_TXT_UTF8, "Strip[0].Mute=1;", 16);
    CHECK_EQ(len, 28 + 16);
    CHECK(memcmp(b, "VBAN", 4) == 0);
    CHECK_EQ(b[4], 0x52); CHECK_EQ(b[5], 0x00); CHECK_EQ(b[6], 0x00); CHECK_EQ(b[7], 0x10); /* spec p.21 */
    CHECK(memcmp(b + 8, "Command1\0\0\0\0\0\0\0\0", 16) == 0);
    CHECK_EQ(b[24], 7); CHECK_EQ(b[25], 0); /* little-endian frame counter */

    vban_name_set(&n, "MIDI1");
    len = vban_build_serial(b, sizeof b, &n, 0, 14, 0, 0, VBAN_SERIAL_MIDI, "\x90\x40\x7f", 3);
    CHECK_EQ(b[4], 0x2E); CHECK_EQ(b[5], 0); CHECK_EQ(b[6], 0); CHECK_EQ(b[7], 0x10); /* spec p.17 (0x2E) */

    vban_name_set(&n, "Register RTP");
    len = vban_build_rt_register(b, sizeof b, &n, 0, 0, 15, NULL, 0);
    CHECK_EQ(len, 28);
    CHECK_EQ(b[4], 0x60); CHECK_EQ(b[5], 0x00); CHECK_EQ(b[6], 0x20); CHECK_EQ(b[7], 0x0F); /* spec p.32 */

    vban_name_set(&n, "VIDEO1");
    len = vban_build_frame_packet(b, sizeof b, &n, 3, 9, 0x1234, VBAN_FRAME_CONTINUE, 0, "x", 1);
    CHECK_EQ(b[4], 0x89); CHECK_EQ(b[5], 0x34); CHECK_EQ(b[6], 0x12); CHECK_EQ(b[7], 0x02); /* spec p.25 */

    memset(&h, 0, sizeof h);
    h.format_SR = 3; h.format_nbs = 255; h.format_nbc = 1; h.format_bit = 1;
    vban_name_set(&h.name, "Stream1");
    h.nu_frame = 0xDEADBEEF;
    vban_header_write(b, &h);
    vban_header_read(b, &h2);
    CHECK(memcmp(&h.name, &h2.name, 16) == 0);
    CHECK_EQ(h2.nu_frame, 0xDEADBEEFu);
    CHECK_EQ(b[24], 0xEF); CHECK_EQ(b[27], 0xDE);
    CHECK_EQ(VBAN_PACKET_MAX_SIZE, 1464);
}

static void test_tables(void)
{
    CHECK_EQ(vban_sr_to_index(48000), 3);
    CHECK_EQ(vban_sr_to_index(44100), 16);
    CHECK_EQ(vban_sr_to_index(705600), 20);
    CHECK_EQ(vban_sr_to_index(47999), -1);
    CHECK_EQ(vban_sr_from_index(21), 0);
    CHECK_EQ(vban_bps_to_index(256000), 18);
    CHECK_EQ(vban_bps_to_index(115200), 14);
    CHECK_EQ(vban_bps_from_index(24), 3000000);
    CHECK_EQ(vban_mbps_table[24], 800);
    CHECK_EQ(vban_max_samples(2, VBAN_DATATYPE_S16), 256);
    CHECK_EQ(vban_max_samples(8, VBAN_DATATYPE_S16), 89);   /* spec p.7 */
    CHECK_EQ(vban_max_samples(2, VBAN_DATATYPE_S24), 239);
    CHECK_EQ(vban_max_samples(256, VBAN_DATATYPE_F64), 0);
    CHECK_EQ(vban_max_samples(2, VBAN_DATATYPE_S12), 0);
}

static void test_parse(void)
{
    uint8_t b[2000];
    vban_name_t n;
    vban_packet_t p;
    vban_name_set(&n, "Stream1");
    vban_build_audio_header(b, &n, 48000, 256, 2, VBAN_DATATYPE_S16, 42);
    memset(b + 28, 0, 1024);
    CHECK_EQ(vban_parse(b, 28 + 1024, &p, 0), VBAN_OK);
    CHECK_EQ(p.sample_rate, 48000); CHECK_EQ(p.samples, 256); CHECK_EQ(p.channels, 2);
    CHECK_EQ(p.datatype, VBAN_DATATYPE_S16); CHECK_EQ(p.nu_frame, 42); CHECK_EQ(p.data_len, 1024);
    CHECK_EQ(vban_parse(b, 28 + 1023, &p, 0), VBAN_E_SIZE);
    CHECK_EQ(vban_parse(b, 27, &p, 0), VBAN_E_SHORT);
    b[0] = 'X'; CHECK_EQ(vban_parse(b, 28 + 1024, &p, 0), VBAN_E_MAGIC); b[0] = 'V';
    b[4] = 21; CHECK_EQ(vban_parse(b, 28 + 1024, &p, 0), VBAN_E_FORMAT); b[4] = 3;
    b[7] |= 0x08; CHECK_EQ(vban_parse(b, 28 + 1024, &p, 0), VBAN_E_FORMAT); b[7] &= ~0x08;
    b[7] = VBAN_CODEC_VBCA | 1; CHECK_EQ(vban_parse(b, 28 + 1024, &p, 0), VBAN_E_UNSUPPORTED);
    CHECK_EQ(p.codec, VBAN_CODEC_VBCA);
    b[7] = VBAN_DATATYPE_S12; CHECK_EQ(vban_parse(b, 28 + 100, &p, 0), VBAN_E_UNSUPPORTED);
    b[7] = 1;
    CHECK_EQ(vban_parse(b, 1465, &p, 0), VBAN_E_OVERSIZE);
    /* 256 channels x 1 sample x u8 */
    vban_build_audio_header(b, &n, 48000, 1, 256, VBAN_DATATYPE_U8, 0);
    CHECK_EQ(vban_parse(b, 28 + 256, &p, 0), VBAN_OK);
    CHECK_EQ(p.channels, 256);
    /* service strict: SR bits must be zero */
    vban_build_ping(b, sizeof b, 1, 0, NULL, NULL);
    CHECK_EQ(vban_parse(b, 28, &p, VBAN_PARSE_STRICT), VBAN_OK);
    b[4] |= 1;
    CHECK_EQ(vban_parse(b, 28, &p, VBAN_PARSE_STRICT), VBAN_E_FORMAT);
    CHECK_EQ(vban_parse(b, 28, &p, 0), VBAN_OK);
}

static void test_names(void)
{
    vban_name_t a;
    uint8_t w[16];
    vban_name_set(&a, "Stream1");
    memcpy(w, "Stream1\0GARBAGE!", 16);
    CHECK(vban_name_eq(&a, w));
    memcpy(w, "Stream12\0\0\0\0\0\0\0\0", 16);
    CHECK(!vban_name_eq(&a, w));
    memcpy(w, "stream1\0\0\0\0\0\0\0\0\0", 16);
    CHECK(!vban_name_eq(&a, w));
    CHECK(vban_name_eq_nocase(&a, w));
    vban_name_set(&a, "ABCDEFGHIJKLMNOPQRS"); /* truncated to 16 */
    memcpy(w, "ABCDEFGHIJKLMNOP", 16);
    CHECK(vban_name_eq(&a, w));
    {
        char s[17];
        vban_name_get(&a, s);
        CHECK(strcmp(s, "ABCDEFGHIJKLMNOP") == 0);
    }
}

static void test_seq(void)
{
    vban_seq_t s;
    uint32_t lost;
    vban_seq_init(&s, 32);
    CHECK_EQ(vban_seq_update(&s, 0xFFFFFFFE, &lost), VBAN_SEQ_FIRST);
    CHECK_EQ(vban_seq_update(&s, 0xFFFFFFFF, &lost), VBAN_SEQ_OK);
    CHECK_EQ(vban_seq_update(&s, 0, &lost), VBAN_SEQ_OK);            /* wrap */
    CHECK_EQ(vban_seq_update(&s, 3, &lost), VBAN_SEQ_GAP); CHECK_EQ(lost, 2);
    CHECK_EQ(vban_seq_update(&s, 2, &lost), VBAN_SEQ_LATE);
    CHECK_EQ(vban_seq_update(&s, 3, &lost), VBAN_SEQ_LATE);          /* duplicate */
    CHECK_EQ(vban_seq_update(&s, 4, &lost), VBAN_SEQ_OK);
    /* big jumps must be confirmed by the next packet (SEC-004) */
    CHECK_EQ(vban_seq_update(&s, 1000, &lost), VBAN_SEQ_SUSPECT);
    CHECK_EQ(vban_seq_update(&s, 1001, &lost), VBAN_SEQ_RESET);      /* confirmed */
    CHECK_EQ(vban_seq_update(&s, 1002, &lost), VBAN_SEQ_OK);
    CHECK_EQ(vban_seq_update(&s, 0, &lost), VBAN_SEQ_SUSPECT);       /* sender restarted from 0 ... */
    CHECK_EQ(vban_seq_update(&s, 1, &lost), VBAN_SEQ_RESET);         /* ... confirmed */
    CHECK_EQ(vban_seq_update(&s, 2, &lost), VBAN_SEQ_OK);
    CHECK_EQ(vban_seq_update(&s, 3, &lost), VBAN_SEQ_OK);
    CHECK_EQ(vban_seq_update(&s, 4, &lost), VBAN_SEQ_OK);
    CHECK_EQ(vban_seq_update(&s, 5, &lost), VBAN_SEQ_OK);
    CHECK_EQ(vban_seq_update(&s, 6, &lost), VBAN_SEQ_OK);
    CHECK_EQ(vban_seq_update(&s, 7, &lost), VBAN_SEQ_OK);
    /* long genuine loss: first packet after it is held, the next confirms with the full count */
    CHECK_EQ(vban_seq_update(&s, 20, &lost), VBAN_SEQ_SUSPECT);
    CHECK_EQ(vban_seq_update(&s, 21, &lost), VBAN_SEQ_GAP); CHECK_EQ(lost, 13);
    /* a lone spoofed counter inside the window does not derail the stream */
    CHECK_EQ(vban_seq_update(&s, 22 + 30, &lost), VBAN_SEQ_SUSPECT);
    CHECK_EQ(vban_seq_update(&s, 22, &lost), VBAN_SEQ_OK);
    CHECK_EQ(vban_seq_update(&s, 23, &lost), VBAN_SEQ_OK);
    /* two consecutive spoofed counters are accepted, but the real stream
     * continuing makes the tracker resync back to it */
    CHECK_EQ(vban_seq_update(&s, 500, &lost), VBAN_SEQ_SUSPECT);
    CHECK_EQ(vban_seq_update(&s, 501, &lost), VBAN_SEQ_RESET);
    CHECK_EQ(vban_seq_update(&s, 24, &lost), VBAN_SEQ_LATE);         /* one stray old packet: held */
    CHECK_EQ(vban_seq_update(&s, 25, &lost), VBAN_SEQ_RESET);        /* two in a row: real stream is back */
    CHECK_EQ(vban_seq_update(&s, 26, &lost), VBAN_SEQ_OK);
    CHECK_EQ(vban_seq_update(&s, 502, &lost), VBAN_SEQ_SUSPECT);     /* attacker's next one is held */
    CHECK_EQ(vban_seq_update(&s, 27, &lost), VBAN_SEQ_OK);

    /* regression: a stream with redundant copies (each packet resent one slot
     * later) after a long loss must not lock up in SUSPECT (netsim dup:1) */
    {
        uint32_t f, rejected = 0;
        vban_seq_init(&s, 32);
        vban_seq_update(&s, 0, &lost);
        vban_seq_update(&s, 1, &lost);
        /* frames 2..9 lost with their copies; then originals + copies of the previous */
        for (f = 10; f < 40; ++f) {
            if (vban_seq_update(&s, f, &lost) == VBAN_SEQ_SUSPECT) ++rejected;
            if (vban_seq_update(&s, f - 1, &lost) == VBAN_SEQ_SUSPECT) ++rejected;
        }
        CHECK(rejected <= 2);
        CHECK_EQ(s.expected, 40);
        /* same with copies 6 slots behind (wider than VBAN_SEQ_CONFIRM_GAP) */
        vban_seq_init(&s, 32);
        vban_seq_update(&s, 0, &lost);
        rejected = 0;
        for (f = 12; f < 60; ++f) {
            if (vban_seq_update(&s, f, &lost) == VBAN_SEQ_SUSPECT) ++rejected;
            if (vban_seq_update(&s, f - 6, &lost) == VBAN_SEQ_SUSPECT) ++rejected;
        }
        CHECK(rejected <= 3);
        CHECK_EQ(s.expected, 60);
    }
}

static void test_ping0(void)
{
    vban_ping0_t p, q;
    uint8_t w[VBAN_PING0_SIZE], pkt[VBAN_PING0_PACKET_SIZE];
    vban_packet_t pk;
    memset(&p, 0, sizeof p);
    p.bit_type = VBAN_PING_TYPE_RECEPTOR | VBAN_PING_TYPE_TRANSMITTER;
    p.bit_feature = VBAN_PING_FEATURE_AUDIO | VBAN_PING_FEATURE_TXT;
    p.prefered_rate = 48000; p.min_rate = 6000; p.max_rate = 705600;
    p.color_rgb = 0x00FF8000;
    p.version[0] = 1; p.version[3] = 9;
    p.gps_position[0] = 45; p.gps_position[1] = 0; p.gps_position[2] = 30; /* binary with NUL */
    strcpy(p.lang_code, "TR");
    strcpy(p.distant_ip, "10.0.0.1"); p.distant_port = 6980;
    strcpy(p.device_name, "Dev");
    strcpy(p.manufacturer_name, "Maker");
    strcpy(p.application_name, "App");
    strcpy(p.host_name, "host");
    strcpy(p.user_name, "Kullan\xc4\xb1\x63\xc4\xb1");
    memset(p.user_comment, 'c', sizeof p.user_comment); /* fills all 128, no NUL */
    vban_ping0_encode(&p, w);
    CHECK_EQ(vban_rd32(w + 0), 3);
    CHECK_EQ(vban_rd32(w + 12), 48000);
    CHECK_EQ(w[30], 0); CHECK_EQ(w[31], 9);
    CHECK_EQ(w[34], 30);
    CHECK(memcmp(w + 48, "TR", 2) == 0);
    CHECK(memcmp(w + 128, "10.0.0.1", 8) == 0);
    CHECK_EQ(vban_rd16(w + 160), 6980);
    CHECK(memcmp(w + 164, "Dev", 4) == 0);
    CHECK(memcmp(w + 228, "Maker", 6) == 0);
    CHECK(memcmp(w + 292, "App", 4) == 0);
    CHECK(memcmp(w + 356, "host", 5) == 0);
    CHECK_EQ(w[548], 'c'); CHECK_EQ(w[675], 'c');
    CHECK_EQ(vban_ping0_decode(w, sizeof w, &q), VBAN_OK);
    CHECK(strcmp(q.device_name, "Dev") == 0);
    CHECK(strcmp(q.user_name, p.user_name) == 0);
    CHECK_EQ(strlen(q.user_comment), 127); /* NUL terminated in decoded form */
    CHECK_EQ(q.max_rate, 705600);
    CHECK_EQ(q.gps_position[2], 30);
    CHECK_EQ(vban_build_ping(pkt, sizeof pkt, 77, 1, NULL, &p), 704);
    CHECK_EQ(vban_parse(pkt, 704, &pk, VBAN_PARSE_STRICT), VBAN_OK);
    CHECK_EQ(pk.protocol, VBAN_PROTOCOL_SERVICE);
    CHECK(vban_service_is_reply(&pk));
    CHECK_EQ(vban_service_type(&pk), VBAN_SERVICE_IDENTIFICATION);
    CHECK_EQ(pk.nu_frame, 77);
    CHECK_EQ(vban_ping0_decode(pkt + 28, 100, &q), VBAN_E_SHORT); /* truncated: tail zeroed */
    CHECK_EQ(q.max_rate, 705600);
    CHECK_EQ(q.device_name[0], 0);
}

/* =================================================== PCM */

/* Public helpers that had no coverage (found via the knowledge graph:
 * symbols with no inbound edges from tests). */
static void test_small_helpers(void)
{
    uint8_t b[64];
    vban_name_t n;
    float in3[4 * 3], a[4], c3[4], out3[4 * 3], l[5], r[5], st[10];
    float *planar3[3], *planar2[2];
    const float *cplanar3[3];
    unsigned i;

    /* vban_is_vban: magic + minimum length */
    vban_name_set(&n, "x");
    vban_build_audio_header(b, &n, 48000, 1, 1, VBAN_DATATYPE_S16, 0);
    CHECK(vban_is_vban(b, 28));
    CHECK(!vban_is_vban(b, 27));
    b[3] = 'M';
    CHECK(!vban_is_vban(b, 28));

    /* vban_pcm_bytes: samples * channels * size; 0 for the undefined 10/12-bit layouts */
    CHECK_EQ(vban_pcm_bytes(256, 2, VBAN_DATATYPE_S16), 1024);
    CHECK_EQ(vban_pcm_bytes(239, 2, VBAN_DATATYPE_S24), 1434);
    CHECK_EQ(vban_pcm_bytes(1, 256, VBAN_DATATYPE_F64), 2048);
    CHECK_EQ(vban_pcm_bytes(64, 2, VBAN_DATATYPE_S12), 0);

    /* deinterleave, generic path (3 ch) with a NULL output to skip */
    for (i = 0; i < 12; ++i) in3[i] = (float)i;
    planar3[0] = a; planar3[1] = NULL; planar3[2] = c3;
    vban_f32_deinterleave(in3, planar3, 3, 4);
    CHECK(a[0] == 0.0f && a[3] == 9.0f && c3[0] == 2.0f && c3[3] == 11.0f);
    /* interleave back: NULL input channel becomes silence */
    cplanar3[0] = a; cplanar3[1] = NULL; cplanar3[2] = c3;
    vban_f32_interleave(cplanar3, out3, 3, 4);
    CHECK(out3[0] == 0.0f && out3[1] == 0.0f && out3[2] == 2.0f && out3[9] == 9.0f && out3[10] == 0.0f && out3[11] == 11.0f);
    /* stereo fast path round trip */
    for (i = 0; i < 10; ++i) st[i] = (float)i * 0.5f;
    planar2[0] = l; planar2[1] = r;
    vban_f32_deinterleave(st, planar2, 2, 5);
    CHECK(l[4] == 4.0f && r[4] == 4.5f);
    {
        float back[10];
        const float *cp2[2];
        cp2[0] = l; cp2[1] = r;
        vban_f32_interleave(cp2, back, 2, 5);
        CHECK(memcmp(back, st, sizeof back) == 0);
    }
    /* stereo with one NULL output takes the generic path, other channel still filled */
    planar2[0] = NULL; planar2[1] = r;
    memset(r, 0, sizeof r);
    vban_f32_deinterleave(st, planar2, 2, 5);
    CHECK(r[2] == 2.5f);
}

static void test_pcm_hostile_floats(void)
{
    /* hostile float samples (SEC-005): NaN -> 0, +-Inf / huge -> +-1, no UB on int conversion */
        float bad[6], got[6];
        int16_t g16[6];
        uint8_t w8[6 * 8];
        unsigned k;
        bad[0] = NAN; bad[1] = INFINITY; bad[2] = -INFINITY; bad[3] = 1e30f; bad[4] = -1e30f; bad[5] = 0.25f;
        for (k = 0; k < 6; ++k) { uint32_t u; memcpy(&u, &bad[k], 4); vban_wr32(w8 + 4 * k, u); }
        vban_pcm_to_f32(w8, VBAN_DATATYPE_F32, got, 6);
        CHECK(got[0] == 0.0f); CHECK(got[1] == 0.0f || got[1] == 1.0f); CHECK(got[2] == 0.0f || got[2] == -1.0f);
        CHECK(got[3] == 1.0f); CHECK(got[4] == -1.0f); CHECK(got[5] == 0.25f);
        vban_pcm_to_s16(w8, VBAN_DATATYPE_F32, g16, 6);
        CHECK_EQ(g16[0], 0); CHECK_EQ(g16[3], 32767); CHECK_EQ(g16[4], -32768); CHECK_EQ(g16[5], 8192);
        /* encode through the SDK's own f64 writer (a hand-written memcpy of NaN
         * doubles here hits a GCC 15 -O3 internal compiler error) */
        vban_pcm_from_f32(bad, w8, VBAN_DATATYPE_F64, 6);
        vban_pcm_to_f32(w8, VBAN_DATATYPE_F64, got, 6);
        CHECK(got[0] == 0.0f); CHECK(got[3] == 1.0f); CHECK(got[4] == -1.0f);
        { float *pl[1]; pl[0] = got; vban_pcm_from_f32(bad, w8, VBAN_DATATYPE_F32, 6);
          vban_pcm_to_f32_planar(w8, VBAN_DATATYPE_F32, 1, 6, pl); CHECK(got[0] == 0.0f && got[3] == 1.0f); }
        /* app-side NaN into an integer wire format: defined result, no UB */
        vban_pcm_from_f32(bad, w8, VBAN_DATATYPE_S16, 6);
        vban_pcm_from_f32(bad, w8, VBAN_DATATYPE_S24, 6);
        CHECK(1);
    }


static void test_pcm(void)
{
    static const unsigned types[] = { VBAN_DATATYPE_U8, VBAN_DATATYPE_S16, VBAN_DATATYPE_S24,
                                      VBAN_DATATYPE_S32, VBAN_DATATYPE_F32, VBAN_DATATYPE_F64 };
    static const double tol[] = { 1.0 / 128, 1.0 / 32768, 1.0 / 8388608, 1e-7, 0, 0 };
    float in[64], out[64];
    uint8_t wire[64 * 8 + 1];
    int16_t s16[64], s16b[64];
    unsigned t, i;
    for (i = 0; i < 64; ++i) in[i] = (float)sin(i * 0.37) * 0.99f;
    in[0] = -1.0f; in[1] = 0.0f;
    for (t = 0; t < 6; ++t) {
        double maxe = 0;
        CHECK_EQ(vban_pcm_from_f32(in, wire + 1, types[t], 64), VBAN_OK); /* odd address: unaligned */
        CHECK_EQ(vban_pcm_to_f32(wire + 1, types[t], out, 64), VBAN_OK);
        for (i = 0; i < 64; ++i) if (fabs(out[i] - in[i]) > maxe) maxe = fabs(out[i] - in[i]);
        CHECK(maxe <= tol[t] * 0.5 + 1e-9);
    }
    /* saturation + exact s16 round trip */
    in[0] = 2.0f; in[1] = -2.0f;
    vban_pcm_from_f32(in, wire, VBAN_DATATYPE_S16, 2);
    CHECK_EQ((int16_t)vban_rd16(wire), 32767);
    CHECK_EQ((int16_t)vban_rd16(wire + 2), -32768);
    vban_pcm_from_f32(in, wire, VBAN_DATATYPE_S32, 2);
    CHECK_EQ((int32_t)vban_rd32(wire), INT32_MAX);
    CHECK_EQ((int32_t)vban_rd32(wire + 4), INT32_MIN);
    vban_pcm_from_f32(in, wire, VBAN_DATATYPE_U8, 2);
    CHECK_EQ(wire[0], 255); CHECK_EQ(wire[1], 0);
    for (i = 0; i < 64; ++i) s16[i] = (int16_t)(i * 1023 - 32768);
    vban_pcm_from_s16(s16, wire, VBAN_DATATYPE_F32, 64);
    vban_pcm_to_s16(wire, VBAN_DATATYPE_F32, s16b, 64);
    CHECK(memcmp(s16, s16b, sizeof s16) == 0);
    vban_pcm_from_s16(s16, wire, VBAN_DATATYPE_S24, 64);
    vban_pcm_to_s16(wire, VBAN_DATATYPE_S24, s16b, 64);
    CHECK(memcmp(s16, s16b, sizeof s16) == 0);
    /* s24 sign extension */
    wire[0] = 0x00; wire[1] = 0x00; wire[2] = 0x80;
    vban_pcm_to_f32(wire, VBAN_DATATYPE_S24, out, 1);
    CHECK_NEAR(out[0], -1.0, 1e-9);
    /* convert any->any */
    for (i = 0; i < 64; ++i) in[i] = (float)i / 64.0f - 0.5f;
    vban_pcm_from_f32(in, wire, VBAN_DATATYPE_S24, 64);
    CHECK_EQ(vban_pcm_convert(wire, VBAN_DATATYPE_S24, wire + 200, VBAN_DATATYPE_F32, 64), VBAN_OK);
    vban_pcm_to_f32(wire + 200, VBAN_DATATYPE_F32, out, 64);
    CHECK_NEAR(out[10], in[10], 1e-6);
    CHECK_EQ(vban_pcm_convert(wire, VBAN_DATATYPE_S10, wire + 200, VBAN_DATATYPE_F32, 4), VBAN_E_UNSUPPORTED);
    /* gain with saturation */
    s16[0] = 20000; s16[1] = -20000; s16[2] = 100;
    vban_pcm_from_s16(s16, wire, VBAN_DATATYPE_S16, 3);
    vban_pcm_gain(wire, VBAN_DATATYPE_S16, 3, 2.0f);
    CHECK_EQ((int16_t)vban_rd16(wire), 32767);
    CHECK_EQ((int16_t)vban_rd16(wire + 2), -32768);
    CHECK_EQ((int16_t)vban_rd16(wire + 4), 200);
    vban_pcm_from_s16(s16, wire, VBAN_DATATYPE_S24, 3);
    vban_pcm_gain(wire, VBAN_DATATYPE_S24, 3, 0.5f);
    vban_pcm_to_s16(wire, VBAN_DATATYPE_S24, s16b, 3);
    CHECK_EQ(s16b[0], 10000); CHECK_EQ(s16b[2], 50);
    CHECK_NEAR(vban_pcm_peak(wire, VBAN_DATATYPE_S24, 3), 10000.0 / 32768, 1e-6);
    /* planar */
    {
        float l[4] = { .1f, .2f, .3f, .4f }, r[4] = { -.1f, -.2f, -.3f, -.4f }, l2[4], r2[4];
        const float *src[2] = { l, r };
        float *dst[2] = { l2, r2 };
        vban_pcm_from_f32_planar(src, 2, 4, wire, VBAN_DATATYPE_S24);
        vban_pcm_to_f32_planar(wire, VBAN_DATATYPE_S24, 2, 4, dst);
        CHECK_NEAR(l2[3], .4, 1e-6); CHECK_NEAR(r2[2], -.3, 1e-6);
    }
}

/* =================================================== jitter buffer */

/* Build an s16 stereo packet whose samples are a running counter. */
static size_t mk_audio(uint8_t *b, uint32_t frame, unsigned samples, unsigned ch, uint32_t *counter, uint32_t rate)
{
    vban_name_t n;
    unsigned i;
    vban_name_set(&n, "jb");
    vban_build_audio_header(b, &n, rate, samples, ch, VBAN_DATATYPE_S16, frame);
    for (i = 0; i < samples * ch; i += ch) {
        unsigned c;
        for (c = 0; c < ch; ++c) vban_wr16(b + 28 + 2 * (i + c), (uint16_t)(int16_t)((*counter % 2000) + c));
        (*counter)++;
    }
    return 28 + (size_t)samples * ch * 2;
}

static uint32_t g_c4;

static void test_jitter_basic(void)
{
    static float ring[4096 * 2];
    vban_jb_t jb;
    vban_jb_config_t cfg;
    vban_packet_t p;
    vban_jb_stats_t st;
    uint8_t b[VBAN_PACKET_MAX_SIZE];
    float out[128 * 2];
    uint32_t counter = 0, f, expect = 0;
    int k, ok = 1;
    memset(&cfg, 0, sizeof cfg);
    cfg.channels = 2; cfg.drift = VBAN_JB_DRIFT_NONE; cfg.target_frames = 256; cfg.adaptive = 0;
    CHECK_EQ(vban_jb_init(&jb, &cfg, ring, 4096), VBAN_OK);
    CHECK_EQ(vban_jb_init(&jb, &cfg, ring, 3000), VBAN_E_ARG); /* not power of 2 */
    vban_jb_init(&jb, &cfg, ring, 4096);

    CHECK_EQ(vban_jb_pull_f32(&jb, out, 128), 0); /* priming -> silence */
    CHECK_EQ(out[0], 0.0f);
    for (f = 0; f < 4; ++f) {
        size_t len = mk_audio(b, f, 128, 2, &counter, 48000);
        vban_parse(b, len, &p, 0);
        CHECK_EQ(vban_jb_push(&jb, &p), VBAN_OK);
    }
    /* bit exact passthrough (s16 -> f32 -> s16) */
    for (k = 0; k < 3; ++k) {
        unsigned i;
        CHECK_EQ(vban_jb_pull_f32(&jb, out, 128), 128);
        for (i = 0; i < 128; ++i, ++expect)
            if ((int)lrintf(out[2 * i] * 32768.0f) != (int)(expect % 2000) ||
                (int)lrintf(out[2 * i + 1] * 32768.0f) != (int)(expect % 2000) + 1) ok = 0;
    }
    CHECK(ok);
    vban_jb_get_stats(&jb, &st);
    CHECK_EQ(st.packets, 4); CHECK_EQ(st.fill_frames, 128); CHECK_EQ(st.lost, 0);

    /* late + duplicate dropped */
    {
        size_t len = mk_audio(b, 2, 128, 2, &counter, 48000);
        vban_parse(b, len, &p, 0);
        vban_jb_push(&jb, &p);
        vban_jb_get_stats(&jb, &st);
        CHECK_EQ(st.dup, 1);          /* a copy of a packet we had: duplicate, not late */
        CHECK_EQ(st.late, 0);
        CHECK_EQ(st.fill_frames, 128);
    }
    /* gap of 2 packets: the packet lands in its own slot, the hole stays open
     * (concealment happens at playout time, not on arrival) */
    {
        size_t len;
        uint32_t c6 = counter + 2 * 128, c4 = counter;
        g_c4 = c4;
        counter = c6;
        len = mk_audio(b, 6, 128, 2, &counter, 48000);
        vban_parse(b, len, &p, 0);
        vban_jb_push(&jb, &p);
        vban_jb_get_stats(&jb, &st);
        CHECK_EQ(st.lost, 2);
        CHECK_EQ(st.concealed_frames, 0);
        CHECK_EQ(st.fill_frames, 128 + 256 + 128);
        /* packet 4 arrives late but before its playout time: it fills its hole */
        counter = c4;
        len = mk_audio(b, 4, 128, 2, &counter, 48000);
        vban_parse(b, len, &p, 0);
        vban_jb_push(&jb, &p);
        vban_jb_push(&jb, &p);                       /* and a redundant copy is dropped */
        vban_jb_get_stats(&jb, &st);
        CHECK_EQ(st.repaired, 1);
        CHECK_EQ(st.dup, 2);
        counter = c6 + 128;
    }
    /* playout: packet 3, 4 (repaired, bit exact), 5 concealed, 6 fades in */
    {
        unsigned i;
        int ok4 = 1;
        uint32_t e4 = g_c4;      /* counter value of packet 4's first frame */
        vban_jb_pull_f32(&jb, out, 128);             /* packet 3 */
        vban_jb_pull_f32(&jb, out, 128);             /* packet 4 */
        for (i = 0; i < 128; ++i)
            if ((int)lrintf(out[2 * i] * 32768.0f) != (int)((e4 + i) % 2000)) ok4 = 0;
        CHECK(ok4);
        CHECK_EQ(vban_jb_pull_f32(&jb, out, 128), 128); /* packet 5: concealed, clock keeps running */
        vban_jb_get_stats(&jb, &st);
        CHECK_EQ(st.concealed_frames, 128);
        CHECK_EQ(st.underruns, 0);
        CHECK(fabsf(out[2 * 127]) < fabsf(out[0]) || out[0] == 0.0f); /* decays */
        vban_jb_pull_f32(&jb, out, 128);             /* packet 6, fading in */
        CHECK(fabsf(out[0]) < 0.05f);
    }
    /* starvation: nothing buffered -> playout holds (counts one underrun) and
     * keeps playing concealment; audio that arrives later is still on time */
    {
        size_t len, got;
        got = vban_jb_pull_f32(&jb, out, 128);
        CHECK_EQ(got, 128);
        vban_jb_get_stats(&jb, &st);
        CHECK_EQ(st.underruns, 1);
        len = mk_audio(b, 7, 128, 2, &counter, 48000);
        vban_parse(b, len, &p, 0);
        vban_jb_push(&jb, &p);
        len = mk_audio(b, 8, 128, 2, &counter, 48000);
        vban_parse(b, len, &p, 0);
        vban_jb_push(&jb, &p);
        len = mk_audio(b, 9, 128, 2, &counter, 48000);
        vban_parse(b, len, &p, 0);
        vban_jb_push(&jb, &p);
        vban_jb_get_stats(&jb, &st);
        CHECK_EQ(st.late, 0);                        /* nothing arrived after its playout time */
        vban_jb_pull_f32(&jb, out, 128);
        vban_jb_get_stats(&jb, &st);
        CHECK_EQ(st.underruns, 1);
        CHECK(!jb.stalled);
    }
    /* sender restart resyncs without counting loss */
    {
        uint32_t lost_before;
        size_t len;
        vban_jb_get_stats(&jb, &st);
        lost_before = st.lost;
        len = mk_audio(b, 50000, 128, 2, &counter, 48000);
        vban_parse(b, len, &p, 0);
        vban_jb_push(&jb, &p);
        vban_jb_get_stats(&jb, &st);
        CHECK_EQ(st.resets, 0);       /* one packet is not enough (could be spoofed) */
        CHECK_EQ(st.rejected, 1);
        len = mk_audio(b, 50001, 128, 2, &counter, 48000);
        vban_parse(b, len, &p, 0);
        vban_jb_push(&jb, &p);
        vban_jb_get_stats(&jb, &st);
        CHECK_EQ(st.resets, 1);
        CHECK_EQ(st.lost, lost_before);
    }
    /* a spoofed packet claiming another sample rate does not retune the stream (SEC-006) */
    {
        size_t len;
        int k;
        vban_jb_get_stats(&jb, &st);
        len = mk_audio(b, 50002, 128, 2, &counter, 705600);
        vban_parse(b, len, &p, 0);
        vban_jb_push(&jb, &p);
        len = mk_audio(b, 50002, 128, 2, &counter, 48000);
        vban_parse(b, len, &p, 0);
        vban_jb_push(&jb, &p);
        vban_jb_get_stats(&jb, &st);
        CHECK_EQ(st.stream_rate, 48000);
        /* a genuine rate change (3 packets in a row) is followed */
        for (k = 0; k < VBAN_JB_RATE_CONFIRM; ++k) {
            len = mk_audio(b, 60000 + (uint32_t)k, 128, 2, &counter, 44100);
            vban_parse(b, len, &p, 0);
            vban_jb_push(&jb, &p);
        }
        vban_jb_get_stats(&jb, &st);
        CHECK_EQ(st.stream_rate, 44100);
    }
}

static void test_jitter_remap_s16_adaptive(void)
{
    static int16_t ring[2048 * 2];
    static float ring2[2048 * 2];
    vban_jb_t jb;
    vban_jb_config_t cfg;
    vban_packet_t p;
    vban_jb_stats_t st;
    uint8_t b[VBAN_PACKET_MAX_SIZE];
    int16_t out[64 * 2];
    float fout[64 * 2];
    uint32_t counter = 100, f;
    memset(&cfg, 0, sizeof cfg);
    cfg.channels = 2; cfg.storage = VBAN_JB_S16; cfg.drift = VBAN_JB_DRIFT_NONE; cfg.target_frames = 64;
    cfg.adaptive = 1; cfg.min_frames = 64;
    CHECK_EQ(vban_jb_init(&jb, &cfg, ring, 2048), VBAN_OK);
    /* mono stream into stereo buffer: duplicated */
    {
        size_t len = mk_audio(b, 0, 64, 1, &counter, 48000);
        vban_parse(b, len, &p, 0);
        vban_jb_push(&jb, &p);
        /* adaptive floor = pull (64) + packet (64): one packet is not enough to start */
        CHECK_EQ(vban_jb_pull_s16(&jb, out, 64), 0);
        vban_jb_get_stats(&jb, &st);
        CHECK_EQ(st.target_frames, 128);
        len = mk_audio(b, 1, 64, 1, &counter, 48000);
        vban_parse(b, len, &p, 0);
        vban_jb_push(&jb, &p);
        CHECK_EQ(vban_jb_pull_s16(&jb, out, 64), 64);
        CHECK_EQ(out[0], 100); CHECK_EQ(out[1], 100); CHECK_EQ(out[127], 163);
    }
    /* starvation grows the target */
    vban_jb_pull_s16(&jb, out, 64);
    vban_jb_pull_s16(&jb, out, 64);
    vban_jb_get_stats(&jb, &st);
    CHECK_EQ(st.underruns, 1);
    CHECK(st.target_frames > 128);

    /* 4-ch stream into stereo float buffer: extra channels dropped */
    memset(&cfg, 0, sizeof cfg);
    cfg.channels = 2; cfg.drift = VBAN_JB_DRIFT_NONE; cfg.target_frames = 64;
    vban_jb_init(&jb, &cfg, ring2, 2048);
    counter = 0;
    for (f = 0; f < 2; ++f) {
        size_t len = mk_audio(b, f, 64, 4, &counter, 48000);
        vban_parse(b, len, &p, 0);
        vban_jb_push(&jb, &p);
    }
    vban_jb_pull_f32(&jb, fout, 64);
    CHECK_EQ((int)lrintf(fout[2 * 5 + 1] * 32768), 6);
}

static void test_jitter_resample(void)
{
    /* producer at 48000 + 0.1% feeds consumer pulling at nominal rate: the
     * resampler must hold the fill level without underruns or skips. */
    static float ring[8192 * 2];
    vban_jb_t jb;
    vban_jb_config_t cfg;
    vban_packet_t p;
    vban_jb_stats_t st;
    uint8_t b[VBAN_PACKET_MAX_SIZE];
    float out[128 * 2];
    uint32_t counter = 0, frame = 0;
    double produced = 0, consumed = 0, ratio = 1.001;
    int iter;
    float maxjump = 0, prev = 0;
    memset(&cfg, 0, sizeof cfg);
    cfg.channels = 2; cfg.drift = VBAN_JB_DRIFT_RESAMPLE; cfg.target_frames = 512; cfg.adaptive = 0;
    vban_jb_init(&jb, &cfg, ring, 8192);
    for (iter = 0; iter < 48000 * 30 / 128; ++iter) { /* 30 s of audio */
        consumed += 128;
        while (produced < consumed * ratio + 600) {
            size_t len, i;
            vban_name_t n;
            vban_name_set(&n, "jb");
            vban_build_audio_header(b, &n, 48000, 64, 2, VBAN_DATATYPE_F32, frame++);
            for (i = 0; i < 64; ++i, ++counter) { /* slow ramp: continuity check */
                float v = (float)sin(counter * 0.01);
                uint32_t u;
                memcpy(&u, &v, 4);
                vban_wr32(b + 28 + 8 * i, u);
                vban_wr32(b + 28 + 8 * i + 4, u);
            }
            len = 28 + 64 * 8;
            vban_parse(b, len, &p, 0);
            vban_jb_push(&jb, &p);
            produced += 64;
        }
        vban_jb_pull_f32(&jb, out, 128);
        if (iter > 10) {
            int i;
            for (i = 0; i < 128; ++i) {
                float d = fabsf(out[2 * i] - prev);
                if (d > maxjump) maxjump = d;
                prev = out[2 * i];
            }
        } else prev = out[254];
    }
    vban_jb_get_stats(&jb, &st);
    CHECK_EQ(st.underruns, 0);
    CHECK_EQ(st.skips, 0);
    CHECK(st.ratio > 1.0005f && st.ratio < 1.0015f);  /* tracked the +0.1 % drift */
    CHECK(maxjump < 0.02f);                           /* no discontinuities */
    printf("  ratio %.5f fill %u target %u maxjump %.4f\n", st.ratio, st.fill_frames, st.target_frames, maxjump);
}

/* =================================================== mixer */

static void test_mixer(void)
{
    static uint32_t mem[256];
    vban_mixer_t mx;
    float a[8], b2[8], o0[8], o1[8], inter_in[16], inter_out[16];
    const float *in[2] = { a, b2 };
    float *out[2] = { o0, o1 };
    int i;
    CHECK(vban_mixer_mem_size(2, 2) <= sizeof mem);
    CHECK_EQ(vban_mixer_init(&mx, mem, sizeof mem, 2, 2), 0);
    for (i = 0; i < 8; ++i) { a[i] = 0.25f; b2[i] = 0.5f; }
    vban_mixer_process(&mx, in, out, 8); /* identity, first block ramps from 0 */
    CHECK_NEAR(o0[7], 0.25, 1e-6); CHECK_NEAR(o1[7], 0.5, 1e-6);
    CHECK(o0[0] < 0.25f);
    vban_mixer_process(&mx, in, out, 8);
    CHECK_NEAR(o0[0], 0.25, 1e-6);
    vban_mixer_set_route(&mx, 1, 0, 1.0f);           /* sum b into out0 */
    vban_mixer_process(&mx, in, out, 8);
    vban_mixer_process(&mx, in, out, 8);
    CHECK_NEAR(o0[3], 0.75, 1e-6);
    vban_mixer_set_input_mute(&mx, 0, 1);
    vban_mixer_process(&mx, in, out, 8);
    vban_mixer_process(&mx, in, out, 8);
    CHECK_NEAR(o0[3], 0.5, 1e-6);
    vban_mixer_set_master(&mx, 4.0f, 0);
    vban_mixer_set_limiter(&mx, VBAN_LIMIT_SOFT);
    vban_mixer_process(&mx, in, out, 8);
    vban_mixer_process(&mx, in, out, 8);
    CHECK(o0[3] < 1.0f && o0[3] > 0.9f);
    CHECK(vban_mixer_take_peak(&mx, 0) < 1.0f);
    vban_mixer_set_limiter(&mx, VBAN_LIMIT_HARD);
    vban_mixer_process(&mx, in, out, 8);
    CHECK_NEAR(o0[3], 1.0, 1e-6);
    /* interleaved == planar */
    vban_mixer_set_master(&mx, 1.0f, 0);
    vban_mixer_set_input_mute(&mx, 0, 0);
    vban_mixer_set_limiter(&mx, VBAN_LIMIT_NONE);
    vban_mixer_process(&mx, in, out, 8);
    for (i = 0; i < 8; ++i) { inter_in[2 * i] = a[i]; inter_in[2 * i + 1] = b2[i]; }
    vban_mixer_process_interleaved(&mx, inter_in, inter_out, 8);
    CHECK_NEAR(inter_out[2 * 5], 0.75, 1e-6);
    CHECK_NEAR(inter_out[2 * 5 + 1], 0.5, 1e-6);
    CHECK_NEAR(vban_db_to_gain(-6.0206f), 0.5, 1e-4);
    CHECK_NEAR(vban_gain_to_db(1.0f), 0.0, 1e-6);
    /* SEC-009: absurd sizes are reported, not wrapped */
    if (sizeof(size_t) == 4) CHECK_EQ(vban_mixer_mem_size(65535, 65535), 0);
    CHECK(vban_mixer_mem_size(8, 8) > 0);
    CHECK_EQ(vban_mixer_init(&mx, mem, 0, 65535, 65535), -1);
}

/* =================================================== tx -> rx */

static void test_tx_rx_pipeline(void)
{
    static capture_t cap;
    static float ring[4096 * 2];
    vban_tx_t tx;
    vban_tx_config_t c;
    vban_rx_t rx;
    vban_jb_t jb;
    vban_jb_config_t jc;
    vban_addr_t d = { VBAN_IP(127, 0, 0, 1), 6980 }, d2 = { VBAN_IP(10, 0, 0, 2), 6981 };
    float in[1000 * 2], out[1000 * 2];
    int i, sent, ok = 1;
    vban_packet_t p;
    memset(&cap, 0, sizeof cap);
    memset(&c, 0, sizeof c);
    c.name = "Stream1"; c.sample_rate = 44100; c.channels = 2; c.datatype = VBAN_DATATYPE_S24;
    c.frames_per_packet = 0; c.send = cap_send; c.send_ctx = &cap;
    CHECK_EQ(vban_tx_init(&tx, &c), VBAN_OK);
    CHECK_EQ(tx.fpp, 239);
    vban_tx_add_dest(&tx, &d);
    vban_tx_add_dest(&tx, &d2);
    for (i = 0; i < 2000; ++i) in[i] = (float)((i / 2) % 100) / 128.0f;
    sent = vban_tx_write_f32(&tx, in, 1000);
    CHECK_EQ(sent, 4);                /* 4 * 239 = 956, 44 pending */
    CHECK_EQ(cap.n, 8);               /* 2 destinations */
    CHECK_EQ(vban_tx_flush(&tx), 1);
    CHECK_EQ(cap.n, 10);
    CHECK_EQ(cap.len[0], 28 + 239 * 6);
    CHECK_EQ(cap.len[8], 28 + 44 * 6);
    CHECK(vban_addr_eq(&cap.to[1], &d2));
    for (i = 0; i < cap.n; ++i) {
        CHECK_EQ(vban_parse(cap.pkt[i], cap.len[i], &p, VBAN_PARSE_STRICT), VBAN_OK);
        CHECK_EQ(p.nu_frame, (uint32_t)(i / 2));
        CHECK_EQ(p.sample_rate, 44100);
    }

    memset(&jc, 0, sizeof jc);
    jc.channels = 2; jc.drift = VBAN_JB_DRIFT_NONE; jc.target_frames = 1000;
    vban_jb_init(&jb, &jc, ring, 4096);
    vban_rx_init(&rx, NULL);
    CHECK(vban_rx_subscribe_audio(&rx, "Stream1", VBAN_IP(127, 0, 0, 1), &jb, NULL, NULL) >= 0);
    for (i = 0; i < cap.n; ++i) {
        vban_addr_t from = cap.to[i]; /* pretend it came from the destination address */
        vban_rx_process(&rx, cap.pkt[i], cap.len[i], &from, 0);
    }
    CHECK_EQ(rx.streams[0].packets, 5);   /* only 127.0.0.1 copies */
    CHECK_EQ(rx.stats.unmatched, 5);
    CHECK_EQ(vban_jb_pull_f32(&jb, out, 1000), 1000);
    for (i = 0; i < 2000; ++i) if (fabsf(out[i] - in[i]) > 1e-6f) ok = 0;
    CHECK(ok);
}

static void test_redundancy_repairs_burst(void)
{
    static capture_t cap;
    static uint8_t hist[3 * VBAN_PACKET_MAX_SIZE];
    static float ring[4096 * 2];
    static int16_t in[64 * 2 * 40], out[64 * 2 * 40];
    vban_tx_t tx;
    vban_tx_config_t c;
    vban_jb_t jb;
    vban_jb_config_t jc;
    vban_jb_stats_t st;
    vban_addr_t d = { VBAN_IP(10, 0, 0, 2), 6980 };
    int i, seen[64] = { 0 }, ok = 1;
    memset(&cap, 0, sizeof cap);
    memset(&c, 0, sizeof c);
    c.name = "red"; c.sample_rate = 48000; c.channels = 2; c.datatype = VBAN_DATATYPE_S16;
    c.frames_per_packet = 64; c.send = cap_send; c.send_ctx = &cap;
    CHECK_EQ(vban_tx_init(&tx, &c), VBAN_OK);
    vban_tx_add_dest(&tx, &d);
    CHECK_EQ(vban_tx_set_redundancy(&tx, 3, hist, 100), VBAN_E_NOSPACE);
    CHECK_EQ(vban_tx_set_redundancy(&tx, 3, hist, sizeof hist), VBAN_OK);
    for (i = 0; i < 64 * 2 * 40; ++i) in[i] = (int16_t)(i * 7 - 9000);
    vban_tx_write_s16(&tx, in, 64 * 40);
    CHECK_EQ(tx.stats.packets, 40);
    CHECK_EQ(tx.stats.redundant, 37);
    CHECK_EQ(cap.n, 40 + 37);

    memset(&jc, 0, sizeof jc);
    jc.channels = 2; jc.drift = VBAN_JB_DRIFT_NONE; jc.target_frames = 64 * 40;
    vban_jb_init(&jb, &jc, ring, 4096);
    for (i = 0; i < cap.n; ++i) {
        vban_packet_t p;
        vban_parse(cap.pkt[i], cap.len[i], &p, VBAN_PARSE_STRICT);
        /* burst loss: the originals of packets 10..12 never arrive (their copies do) */
        if (!seen[p.nu_frame & 63]++ && p.nu_frame >= 10 && p.nu_frame <= 12) continue;
        vban_jb_push(&jb, &p);
    }
    vban_jb_get_stats(&jb, &st);
    CHECK_EQ(st.lost, 3);
    CHECK_EQ(st.repaired, 3);
    CHECK_EQ(st.dup, 37 - 3);
    vban_jb_pull_s16(&jb, out, 64 * 40);
    vban_jb_get_stats(&jb, &st);
    CHECK_EQ(st.concealed_frames, 0);
    for (i = 0; i < 64 * 2 * 37; ++i) if (out[i] != in[i]) { ok = 0; break; }
    CHECK(ok);                                /* bit exact through the burst */
}

static void test_rx_ping_and_matching(void)
{
    static capture_t cap;
    vban_rx_t rx;
    vban_rx_config_t rc;
    vban_ping0_t me, got;
    uint8_t b[VBAN_PACKET_MAX_SIZE];
    vban_addr_t from = { VBAN_IP(192, 168, 1, 50), 6980 };
    vban_name_t label;
    vban_packet_t p;
    int i, n;
    memset(&cap, 0, sizeof cap);
    memset(&me, 0, sizeof me);
    me.bit_type = VBAN_PING_TYPE_RECEPTOR;
    strcpy(me.device_name, "SDK test");
    memset(&rc, 0, sizeof rc);
    rc.identity = &me; rc.send = cap_send; rc.send_ctx = &cap;
    vban_rx_init(&rx, &rc);
    vban_name_set(&label, "VBAN SPOT PING");
    n = vban_build_ping(b, sizeof b, 1234, 0, &label, NULL); /* header-only request */
    vban_rx_process(&rx, b, (size_t)n, &from, 1);
    CHECK_EQ(cap.n, 1);
    CHECK_EQ(cap.len[0], 704);
    CHECK(vban_addr_eq(&cap.to[0], &from));
    vban_parse(cap.pkt[0], cap.len[0], &p, VBAN_PARSE_STRICT);
    CHECK(vban_service_is_reply(&p));
    CHECK_EQ(p.nu_frame, 1234);
    CHECK(memcmp(p.name, "VBAN SPOT PING", 14) == 0);
    vban_ping0_decode(p.data, p.data_len, &got);
    CHECK(strcmp(got.device_name, "SDK test") == 0);
    /* per-source limit: the same host gets one reply per VBAN_PING_PER_SOURCE_MS */
    for (i = 0; i < 50; ++i) vban_rx_process(&rx, b, (size_t)n, &from, 2);
    CHECK_EQ(rx.stats.pings_answered, 1);
    CHECK_EQ(rx.stats.pings_limited, 50);
    /* global limit: many (private) sources within one second */
    vban_rx_init(&rx, &rc);
    for (i = 0; i < 50; ++i) {
        vban_addr_t a = { VBAN_IP(192, 168, 2, (unsigned)i + 1), 6980 };
        vban_rx_process(&rx, b, (size_t)n, &a, 5);
    }
    CHECK_EQ(rx.stats.pings_answered, VBAN_PING_REPLY_RATE);
    vban_rx_process(&rx, b, (size_t)n, &from, 5 + 1000000000ull);
    CHECK_EQ(rx.stats.pings_answered, VBAN_PING_REPLY_RATE + 1);
    /* never reflect to broadcast / multicast / port 0 (SEC-003) */
    {
        vban_addr_t bad[3] = { { VBAN_IP_BROADCAST, 6980 }, { VBAN_IP(224, 0, 0, 1), 6980 },
                               { VBAN_IP(192, 168, 1, 9), 0 } };
        vban_rx_init(&rx, &rc);
        cap.n = 0;
        for (i = 0; i < 3; ++i) vban_rx_process(&rx, b, (size_t)n, &bad[i], 10);
        CHECK_EQ(cap.n, 0);
        CHECK_EQ(rx.stats.pings_refused, 3);
    }
    /* public sources: header-only request -> header-only reply (1:1, no amplification),
     * and their own smaller bucket so an internet flood cannot starve LAN discovery */
    {
        vban_rx_init(&rx, &rc);
        cap.n = 0;
        for (i = 0; i < 30; ++i) {
            vban_addr_t a = { VBAN_IP(8, 8, 4, (unsigned)i + 1), 6980 };
            vban_rx_process(&rx, b, (size_t)n, &a, 20);
        }
        CHECK_EQ(cap.n, VBAN_PING_REPLY_RATE_PUBLIC);
        CHECK_EQ(cap.len[0], VBAN_HEADER_SIZE);
        vban_rx_process(&rx, b, (size_t)n, &from, 20); /* LAN still answered, in full */
        CHECK_EQ(cap.n, VBAN_PING_REPLY_RATE_PUBLIC + 1);
        CHECK_EQ(cap.len[cap.n - 1], 704);
        /* a full-size public request gets the full identity (no amplification) */
        {
            uint8_t full[VBAN_PING0_PACKET_SIZE];
            vban_addr_t a = { VBAN_IP(9, 9, 9, 9), 6980 };
            int fn = vban_build_ping(full, sizeof full, 77, 0, NULL, &me);
            vban_rx_init(&rx, &rc);
            cap.n = 0;
            vban_rx_process(&rx, full, (size_t)fn, &a, 30);
            CHECK_EQ(cap.n, 1);
            CHECK_EQ(cap.len[0], 704);
        }
        /* now == 0 no longer disables limiting */
        vban_rx_init(&rx, &rc);
        for (i = 0; i < 10; ++i) vban_rx_process(&rx, b, (size_t)n, &from, 0);
        CHECK_EQ(rx.stats.pings_answered, 1);
    }
    /* replies are not answered */
    vban_build_ping(b, sizeof b, 5, 1, NULL, &me);
    i = cap.n;
    vban_rx_process(&rx, b, 704, &from, 3);
    CHECK_EQ(cap.n, i);
}

static int g_text_hits;
static void on_text(void *u, const vban_packet_t *p, const vban_addr_t *f)
{
    (void)u; (void)f;
    if (p->data_len == 16 && memcmp(p->data, "Strip[0].Mute=1;", 16) == 0) g_text_hits++;
}

static void test_text_serial_frame(void)
{
    static capture_t cap;
    static uint8_t big[5000], rbuf[8192];
    vban_out_t o;
    vban_addr_t to = { VBAN_IP(127, 0, 0, 1), 6980 };
    vban_rx_t rx;
    vban_reasm_t ra;
    vban_packet_t p;
    int i, r = 0;
    const uint8_t *data = NULL;
    size_t len = 0;
    memset(&cap, 0, sizeof cap);
    vban_out_init(&o, "Command1", &to, cap_send, &cap);
    CHECK(vban_send_text(&o, "Strip[0].Mute=1;", -1) > 0);
    vban_rx_init(&rx, NULL);
    vban_rx_subscribe(&rx, "command1", 0, VBAN_PROTOCOL_TXT, on_text, NULL);
    rx.streams[0].nocase = 1;
    vban_rx_process(&rx, cap.pkt[0], cap.len[0], &to, 0);
    CHECK_EQ(g_text_hits, 1);
    {
        char longtxt[1500];
        memset(longtxt, 'a', sizeof longtxt);
        CHECK_EQ(vban_send_text_ex(&o, 18, 0, VBAN_TXT_UTF8, longtxt, 1437), VBAN_E_OVERSIZE);
    }
    /* SysEx 5000 bytes -> 4 multipart serial packets */
    for (i = 0; i < 5000; ++i) big[i] = (uint8_t)(i * 7);
    cap.n = 0;
    vban_out_init(&o, "MIDI1", &to, cap_send, &cap);
    CHECK_EQ(vban_send_midi(&o, big, 5000), 4);
    CHECK_EQ(cap.pkt[0][5] & 0x80, 0x80);
    CHECK_EQ(cap.pkt[3][5] & 0x80, 0);
    CHECK_EQ(cap.pkt[0][7], VBAN_SERIAL_MIDI);
    vban_reasm_init(&ra, rbuf, sizeof rbuf);
    for (i = 0; i < 4; ++i) {
        vban_parse(cap.pkt[i], cap.len[i], &p, VBAN_PARSE_STRICT);
        r = vban_reasm_serial(&ra, &p, &data, &len);
    }
    CHECK_EQ(r, 1); CHECK_EQ(len, 5000);
    CHECK(data && memcmp(data, big, 5000) == 0);
    /* dropped middle fragment -> discarded */
    vban_reasm_init(&ra, rbuf, sizeof rbuf);
    vban_parse(cap.pkt[0], cap.len[0], &p, 0); vban_reasm_serial(&ra, &p, &data, &len);
    vban_parse(cap.pkt[2], cap.len[2], &p, 0); r = vban_reasm_serial(&ra, &p, &data, &len);
    CHECK_EQ(r, 0);
    CHECK_EQ(ra.dropped, 1);
    /* single small message: zero copy */
    cap.n = 0;
    vban_send_midi(&o, "\x90\x3c\x40", 3);
    vban_parse(cap.pkt[0], cap.len[0], &p, 0);
    vban_reasm_init(&ra, rbuf, sizeof rbuf);
    CHECK_EQ(vban_reasm_serial(&ra, &p, &data, &len), 1);
    CHECK(data == p.data && len == 3);

    /* FRAME 5000 bytes -> 4 packets; lose one -> no frame */
    cap.n = 0;
    vban_out_init(&o, "VIDEO1", &to, cap_send, &cap);
    CHECK_EQ(vban_send_frame(&o, 9, VBAN_FRAME_DEFAULT, big, 5000), 4);
    CHECK_EQ(cap.pkt[0][7] & 7, VBAN_FRAME_START);
    CHECK_EQ(cap.pkt[1][7] & 7, VBAN_FRAME_CONTINUE);
    CHECK_EQ(cap.pkt[3][7] & 7, VBAN_FRAME_END);
    vban_reasm_init(&ra, rbuf, sizeof rbuf);
    for (i = 0; i < 4; ++i) {
        vban_parse(cap.pkt[i], cap.len[i], &p, VBAN_PARSE_STRICT);
        r = vban_reasm_frame(&ra, &p);
    }
    CHECK_EQ(r, 1); CHECK_EQ(ra.len, 5000);
    CHECK(memcmp(rbuf, big, 5000) == 0);
    vban_reasm_init(&ra, rbuf, sizeof rbuf);
    for (i = 0; i < 4; ++i) {
        if (i == 2) continue;
        vban_parse(cap.pkt[i], cap.len[i], &p, 0);
        r = vban_reasm_frame(&ra, &p);
    }
    CHECK(r < 0);
    /* single packet frame has START|END */
    cap.n = 0;
    vban_send_frame(&o, 0, 0, "abc", 3);
    CHECK_EQ(cap.pkt[0][7] & 7, VBAN_FRAME_START | VBAN_FRAME_END);
    vban_parse(cap.pkt[0], cap.len[0], &p, 0);
    vban_reasm_init(&ra, rbuf, sizeof rbuf);
    CHECK_EQ(vban_reasm_frame(&ra, &p), 1);
}

static void test_rtp_server(void)
{
    static capture_t cap;
    vban_rtp_client_t slots[2];
    vban_rtp_server_t srv;
    uint8_t b[VBAN_PACKET_MAX_SIZE];
    vban_packet_t p, rp;
    vban_addr_t c1 = { VBAN_IP(10, 0, 0, 1), 5000 }, c2 = { VBAN_IP(10, 0, 0, 2), 5000 },
                c3 = { VBAN_IP(10, 0, 0, 3), 5000 };
    int n;
    memset(&cap, 0, sizeof cap);
    vban_rtp_server_init(&srv, slots, 2, "Voicemeeter RTP", cap_send, &cap);
    vban_rtp_server_offer(&srv, 0);
    n = vban_build_rt_register(b, sizeof b, NULL, 9, 0, 15, NULL, 0);
    vban_parse(b, (size_t)n, &p, 0);
    CHECK_EQ(vban_rtp_server_handle(&srv, &p, &c1, 1000), 1);
    vban_parse(cap.pkt[0], cap.len[0], &rp, 0);
    CHECK_EQ(rp.format_bit, VBAN_RTREG_OK);
    CHECK_EQ(rp.nu_frame, 9);
    CHECK(vban_service_is_reply(&rp));
    vban_rtp_server_handle(&srv, &p, &c2, 1000);
    vban_rtp_server_handle(&srv, &p, &c3, 1000);
    vban_parse(cap.pkt[2], cap.len[2], &rp, 0);
    CHECK_EQ(rp.format_bit, VBAN_RTREG_BUSY);
    n = vban_build_rt_register(b, sizeof b, NULL, 10, 5, 15, NULL, 0); /* id 5 not offered */
    vban_parse(b, (size_t)n, &p, 0);
    vban_rtp_server_handle(&srv, &p, &c1, 1000);
    vban_parse(cap.pkt[3], cap.len[3], &rp, 0);
    CHECK_EQ(rp.format_bit, VBAN_RTREG_NONE);
    cap.n = 0;
    CHECK_EQ(vban_rtp_server_publish(&srv, 0, "state", 5, 2000), 2);
    vban_parse(cap.pkt[0], cap.len[0], &rp, 0);
    CHECK_EQ(vban_service_type(&rp), VBAN_SERVICE_RTPACKET);
    CHECK_EQ(rp.data_len, 5);
    CHECK_EQ(vban_rtp_server_publish(&srv, 0, "state", 5, 16000000001ull), 0); /* expired */

    /* anti-reflection (SEC-001/002) */
    {
        vban_rtp_client_t s4[4];
        vban_addr_t pub = { VBAN_IP(8, 8, 8, 8), 5000 }, bc = { VBAN_IP_BROADCAST, 5000 },
                    p0 = { VBAN_IP(10, 0, 0, 7), 0 };
        vban_rtp_server_init(&srv, s4, 4, "Voicemeeter RTP", cap_send, &cap);
        vban_rtp_server_offer(&srv, 0);
        n = vban_build_rt_register(b, sizeof b, NULL, 1, 0, 255, NULL, 0); /* asks for 255 s */
        vban_parse(b, (size_t)n, &p, 0);
        cap.n = 0;
        vban_rtp_server_handle(&srv, &p, &pub, 1000);  /* spoofed internet victim */
        vban_rtp_server_handle(&srv, &p, &bc, 1000);
        vban_rtp_server_handle(&srv, &p, &p0, 1000);
        CHECK_EQ(cap.n, 0);                             /* no reply, no subscription */
        CHECK_EQ(srv.refused, 3);
        CHECK_EQ(vban_rtp_server_publish(&srv, 0, "x", 1, 2000), 0);
        vban_rtp_server_handle(&srv, &p, &c1, 1000);
        CHECK_EQ(vban_rtp_server_publish(&srv, 0, "x", 1, 1000 + 29000000000ull), 1);
        CHECK_EQ(vban_rtp_server_publish(&srv, 0, "x", 1, 1000 + 31000000000ull), 0); /* capped at 30 s */
        /* one IP cannot take every slot */
        {
            vban_addr_t a = c2;
            unsigned k;
            cap.n = 0;
            for (k = 0; k < 4; ++k) { a.port = (uint16_t)(6000 + k); vban_rtp_server_handle(&srv, &p, &a, 40000000000ull); }
            vban_parse(cap.pkt[2], cap.len[2], &rp, 0);
            CHECK_EQ(rp.format_bit, VBAN_RTREG_BUSY);
            vban_rtp_server_handle(&srv, &p, &c3, 40000000000ull);
            vban_parse(cap.pkt[4], cap.len[4], &rp, 0);
            CHECK_EQ(rp.format_bit, VBAN_RTREG_OK);    /* another host still gets in */
        }
        srv.allow_public = 1;
        cap.n = 0;
        vban_rtp_server_handle(&srv, &p, &pub, 40000000000ull);
        CHECK_EQ(cap.n, 1);                             /* explicit opt-in */
    }
}

static void test_heard(void)
{
    vban_heard_t tab[2];
    uint8_t b[64];
    vban_name_t n;
    vban_packet_t p;
    vban_addr_t a = { 1, 1 }, c = { 2, 2 }, d = { 3, 3 };
    memset(tab, 0, sizeof tab);
    vban_name_set(&n, "s");
    vban_build_audio_header(b, &n, 48000, 1, 1, VBAN_DATATYPE_S16, 0);
    vban_parse(b, 30, &p, 0);
    CHECK_EQ(vban_heard_update(tab, 2, &p, &a, 10), 0);
    CHECK_EQ(vban_heard_update(tab, 2, &p, &a, 11), 0);
    CHECK_EQ(tab[0].packets, 2);
    CHECK_EQ(vban_heard_update(tab, 2, &p, &c, 12), 1);
    CHECK_EQ(vban_heard_update(tab, 2, &p, &d, 13), 0); /* replaces oldest */
    CHECK(vban_addr_eq(&tab[0].from, &d));
    CHECK_EQ(vban_heard_update(tab, 0, &p, &a, 14), VBAN_E_ARG); /* SEC-009: empty table */
    /* address classification used by the responders */
    {
        vban_addr_t u1 = { VBAN_IP(192, 168, 1, 2), 6980 }, u2 = { VBAN_IP(192, 168, 1, 2), 0 },
                    u3 = { VBAN_IP(239, 1, 2, 3), 6980 }, u4 = { 0, 6980 }, u5 = { VBAN_IP(250, 0, 0, 1), 1 };
        CHECK(vban_addr_is_unicast(&u1)); CHECK(!vban_addr_is_unicast(&u2)); CHECK(!vban_addr_is_unicast(&u3));
        CHECK(!vban_addr_is_unicast(&u4)); CHECK(!vban_addr_is_unicast(&u5));
        CHECK(vban_addr_is_private(VBAN_IP(10, 1, 2, 3))); CHECK(vban_addr_is_private(VBAN_IP(172, 31, 0, 1)));
        CHECK(!vban_addr_is_private(VBAN_IP(172, 32, 0, 1))); CHECK(vban_addr_is_private(VBAN_IP(100, 64, 0, 1)));
        CHECK(!vban_addr_is_private(VBAN_IP(100, 128, 0, 1))); CHECK(!vban_addr_is_private(VBAN_IP(8, 8, 8, 8)));
        CHECK(vban_addr_is_private(VBAN_IP(127, 0, 0, 1))); CHECK(vban_addr_is_private(VBAN_IP(169, 254, 3, 4)));
    }
}

/* =================================================== real UDP loopback */

static void test_udp_loopback(void)
{
    vban_socket_t rs = VBAN_SOCKET_INIT, ts = VBAN_SOCKET_INIT;
    vban_tx_t tx;
    vban_tx_config_t c;
    vban_addr_t to;
    static uint8_t bufs[VBAN_NET_BATCH][VBAN_PACKET_MAX_SIZE];
    vban_msg_t msgs[VBAN_NET_BATCH];
    float in[64 * 2 * 10];
    int i, port, got = 0, tries = 0;
    CHECK_EQ(vban_net_init(), VBAN_OK);
    CHECK_EQ(vban_udp_open(&rs, "127.0.0.1", 0, VBAN_SOCK_REUSE), VBAN_OK);
    CHECK_EQ(vban_udp_open(&ts, NULL, 0, VBAN_SOCK_QOS | VBAN_SOCK_BROADCAST), VBAN_OK);
    port = vban_udp_local_port(&rs);
    CHECK(port > 0);
    vban_addr_parse("127.0.0.1", (uint16_t)port, &to);
    memset(&c, 0, sizeof c);
    c.name = "loop"; c.sample_rate = 48000; c.channels = 2; c.datatype = VBAN_DATATYPE_S16;
    c.frames_per_packet = 64; c.send = vban_udp_sink; c.send_ctx = &ts;
    vban_tx_init(&tx, &c);
    vban_tx_add_dest(&tx, &to);
    memset(in, 0, sizeof in);
    CHECK_EQ(vban_tx_write_f32(&tx, in, 640), 10);
    for (i = 0; i < VBAN_NET_BATCH; ++i) { msgs[i].buf = bufs[i]; msgs[i].cap = VBAN_PACKET_MAX_SIZE; }
    while (got < 10 && tries++ < 50) {
        int n = vban_udp_recv_batch(&rs, msgs, VBAN_NET_BATCH, 100);
        if (n > 0) {
            for (i = 0; i < n; ++i) {
                vban_packet_t p;
                CHECK_EQ(vban_parse(msgs[i].buf, msgs[i].len, &p, VBAN_PARSE_STRICT), VBAN_OK);
                CHECK_EQ(p.nu_frame, (uint32_t)(got + i));
            }
            got += n;
        }
    }
    CHECK_EQ(got, 10);
    CHECK_EQ(vban_udp_recv(&rs, bufs[0], VBAN_PACKET_MAX_SIZE, NULL, 10), VBAN_E_TIMEOUT);
    /* oversized datagram is dropped, never handed back cut (SEC-007) */
    {
        static uint8_t big[2000];
        vban_name_t nm;
        int r;
        vban_name_set(&nm, "big");
        vban_build_text(big, sizeof big, &nm, 0, 0, 0, VBAN_TXT_UTF8, "x", 1);
        vban_udp_send(&ts, big, sizeof big, &to);
        r = vban_udp_recv(&rs, bufs[0], VBAN_PACKET_MAX_SIZE, NULL, 500);
        CHECK_EQ(r, VBAN_E_OVERSIZE);
        vban_udp_send(&ts, big, sizeof big, &to);
        r = vban_udp_recv_batch(&rs, msgs, VBAN_NET_BATCH, 500);
        CHECK_EQ(r, 1);
        CHECK_EQ(msgs[0].len, 0);
    }
    vban_udp_close(&rs);
    vban_udp_close(&ts);
    vban_net_cleanup();
}

/* =================================================== fuzz */

static void test_fuzz(void)
{
    static uint8_t b[2048], rbuf[4096];
    static float ring[1024 * 2];
    static capture_t cap;
    vban_rx_t rx;
    vban_rx_config_t rc;
    vban_jb_t jb;
    vban_reasm_t ra;
    vban_ping0_t me, q;
    uint32_t seed = 12345;
    float out[64 * 2];
    long it;
    memset(&me, 0, sizeof me);
    memset(&rc, 0, sizeof rc);
    rc.identity = &me; rc.send = cap_send; rc.send_ctx = &cap;
    vban_rx_init(&rx, &rc);
    vban_jb_init(&jb, NULL, ring, 1024);
    vban_rx_subscribe_audio(&rx, NULL, 0, &jb, NULL, NULL);
    vban_reasm_init(&ra, rbuf, sizeof rbuf);
    for (it = 0; it < 300000; ++it) {
        size_t len, i;
        vban_packet_t p;
        const uint8_t *d;
        size_t dl;
        seed = seed * 1664525u + 1013904223u;
        len = (seed >> 8) % sizeof b;
        for (i = 0; i < len; ++i) { seed = seed * 1664525u + 1013904223u; b[i] = (uint8_t)(seed >> 24); }
        if (len >= 4) memcpy(b, "VBAN", 4);
        if ((seed & 3) == 0 && len > 28) { /* make sizes consistent often */
            b[4] &= 0x1F; b[4] %= 21; b[7] &= 0x07; if (b[7] > 5) b[7] = 1;
            len = 28 + (size_t)(b[5] + 1) * (b[6] + 1) * vban_datatype_size[b[7]];
            if (len > sizeof b) len = 28;
        }
        cap.n = 0;
        vban_rx_process(&rx, b, len, NULL, (uint64_t)it);
        if (vban_parse(b, len, &p, 0) >= 0) {
            if (p.protocol == VBAN_PROTOCOL_FRAME) vban_reasm_frame(&ra, &p);
            if (p.protocol == VBAN_PROTOCOL_SERIAL) vban_reasm_serial(&ra, &p, &d, &dl);
            if (p.protocol == VBAN_PROTOCOL_SERVICE) vban_ping0_decode(p.data, p.data_len, &q);
        }
        if ((it & 15) == 0) vban_jb_pull_f32(&jb, out, 64);
    }
    CHECK(1); /* reaching here under ASan/UBSan is the test */
}

int main(void)
{
    RUN(test_header_and_spec_examples);
    RUN(test_tables);
    RUN(test_parse);
    RUN(test_names);
    RUN(test_seq);
    RUN(test_ping0);
    RUN(test_pcm);
    RUN(test_pcm_hostile_floats);
    RUN(test_small_helpers);
    RUN(test_jitter_basic);
    RUN(test_jitter_remap_s16_adaptive);
    RUN(test_jitter_resample);
    RUN(test_mixer);
    RUN(test_tx_rx_pipeline);
    RUN(test_redundancy_repairs_burst);
    RUN(test_rx_ping_and_matching);
    RUN(test_text_serial_frame);
    RUN(test_rtp_server);
    RUN(test_heard);
    RUN(test_udp_loopback);
    RUN(test_fuzz);
    printf("\n%d checks, %d failures\n", g_checks, g_fail);
    return g_fail ? 1 : 0;
}
