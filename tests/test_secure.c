/*
 * Secure VBAN tests: Noise vectors, certificates, sessions, replay, rekey,
 * responder table, and an end-to-end vban_tx -> seal -> open -> vban_rx run.
 */
#include <stdio.h>
#include <string.h>
#include "vban.h"
#include "vban/vban_secure.h"
#include "noise_xx_vector.h"

static int g_checks, g_fail;
#define CHECK(c) do { ++g_checks; if (!(c)) { ++g_fail; fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #c); } } while (0)

/* ---- Noise_XX_25519_ChaChaPoly_BLAKE2b against the cacophony vector ---- */
static void test_noise_vectors(void)
{
    vban_noise_hs_t I, R;
    uint8_t buf[512], pl[512], is[32], ir[32], rs[32], rr[32], h1[64], h2[64];
    uint64_t ni = 0, nr = 0;
    int i, n;

    vban_noise_init(&I, 1, nxv_init_static, nxv_init_ephemeral, nxv_init_prologue, NXV_PROLOGUE_LEN);
    vban_noise_init(&R, 0, nxv_resp_static, nxv_resp_ephemeral, nxv_init_prologue, NXV_PROLOGUE_LEN);
    for (i = 0; i < 3; ++i) {
        vban_noise_hs_t *w = (i & 1) ? &R : &I, *r = (i & 1) ? &I : &R;
        n = vban_noise_write(w, nxv_payload[i], nxv_plen[i], buf, sizeof(buf));
        CHECK(n == (int)nxv_clen[i] && !memcmp(buf, nxv_cipher[i], nxv_clen[i]));
        n = vban_noise_read(r, buf, (size_t)n, pl, sizeof(pl));
        CHECK(n == (int)nxv_plen[i] && !memcmp(pl, nxv_payload[i], nxv_plen[i]));
    }
    CHECK(!vban_noise_split(&I, is, ir, h1));
    CHECK(!vban_noise_split(&R, rs, rr, h2));
    CHECK(!memcmp(h1, nxv_handshake_hash, 64) && !memcmp(h2, nxv_handshake_hash, 64));
    for (i = 3; i < NXV_NMSG; ++i) {          /* transport messages alternate, responder first */
        int from_r = i & 1;
        vban_noise_encrypt(from_r ? rs : is, from_r ? nr : ni, NULL, 0, nxv_payload[i], nxv_plen[i], buf);
        CHECK(!memcmp(buf, nxv_cipher[i], nxv_clen[i]));
        CHECK(!vban_noise_decrypt(from_r ? ir : rr, from_r ? nr : ni, NULL, 0, buf, nxv_plen[i], pl));
        CHECK(!memcmp(pl, nxv_payload[i], nxv_plen[i]));
        if (from_r) nr++; else ni++;
    }
    /* wrong turn, and a tampered message poisons the state */
    vban_noise_init(&I, 1, nxv_init_static, nxv_init_ephemeral, NULL, 0);
    vban_noise_init(&R, 0, nxv_resp_static, nxv_resp_ephemeral, NULL, 0);
    CHECK(vban_noise_read(&I, buf, 32, pl, sizeof(pl)) == VBAN_E_STATE);
    n = vban_noise_write(&I, NULL, 0, buf, sizeof(buf));
    CHECK(vban_noise_read(&R, buf, (size_t)n, pl, sizeof(pl)) == 0);
    n = vban_noise_write(&R, (const uint8_t *)"x", 1, buf, sizeof(buf));
    buf[40] ^= 1;
    CHECK(vban_noise_read(&I, buf, (size_t)n, pl, sizeof(pl)) == VBAN_E_AUTH);
    CHECK(vban_noise_write(&I, NULL, 0, buf, sizeof(buf)) == VBAN_E_STATE);
    /* a low-order (all-zero) peer key is refused */
    vban_noise_init(&R, 0, nxv_resp_static, nxv_resp_ephemeral, NULL, 0);
    memset(buf, 0, 32);
    CHECK(vban_noise_read(&R, buf, 32, pl, sizeof(pl)) == 0);
    CHECK(vban_noise_write(&R, NULL, 0, buf, sizeof(buf)) == VBAN_E_AUTH);
}

/* ---- a small PKI ---- */
typedef struct {
    uint8_t ca_sk[64], ca_pk[32], ca_bytes[VBAN_CERT_SIZE];
    vban_cert_t ca;
} pki_t;

typedef struct {
    uint8_t sk[32], pk[32], cert[VBAN_CERT_SIZE];
    vban_sec_id_t id;
} dev_t_;

static void pki_new(pki_t *p, const char *name)
{
    CHECK(!vban_ca_keygen(p->ca_sk, p->ca_pk));
    CHECK(!vban_ca_cert(&p->ca, name, p->ca_sk, p->ca_pk, 1000, 4000000000u, 1));
    CHECK(!vban_cert_encode(&p->ca, p->ca_bytes));
}

static void dev_new(dev_t_ *d, const pki_t *p, const char *name, uint8_t role, uint64_t nb, uint64_t na)
{
    vban_cert_t c;
    memset(&c, 0, sizeof(c));
    CHECK(!vban_sec_keygen(d->sk, d->pk));
    strcpy(c.name, name);
    c.role = role;
    c.not_before = nb;
    c.not_after = na;
    c.serial = 42;
    memcpy(c.key, d->pk, 32);
    CHECK(!vban_cert_sign(&c, p->ca_sk, p->ca_pk));
    CHECK(!vban_cert_encode(&c, d->cert));
    CHECK(!vban_sec_id_init(&d->id, d->sk, d->cert, VBAN_CERT_SIZE, p->ca_bytes, VBAN_CERT_SIZE));
}

static void test_certs(void)
{
    pki_t p, q;
    dev_t_ d;
    vban_cert_t c, c2;
    uint8_t enc[VBAN_CERT_SIZE], req[VBAN_CERT_REQ_SIZE], pk[32], role, bin[256];
    char name[VBAN_CERT_NAME_MAX + 1], pem[600];
    unsigned i, bad = 0;
    int n;

    pki_new(&p, "Studio CA");
    pki_new(&q, "Other CA");
    CHECK(!vban_cert_verify(&p.ca, p.ca_pk, 0));
    dev_new(&d, &p, "PC mic", VBAN_CERT_ROLE_TX, 1000, 2000);
    CHECK(!vban_cert_decode(d.cert, VBAN_CERT_SIZE, &c));
    CHECK(!strcmp(c.name, "PC mic") && c.role == VBAN_CERT_ROLE_TX && c.serial == 42);
    CHECK(!vban_cert_verify(&c, p.ca_pk, 0));
    CHECK(!vban_cert_verify(&c, p.ca_pk, 1500));
    CHECK(vban_cert_verify(&c, p.ca_pk, 999) == VBAN_E_CERT);       /* not yet valid */
    CHECK(vban_cert_verify(&c, p.ca_pk, 2001) == VBAN_E_CERT);      /* expired */
    CHECK(vban_cert_verify(&c, q.ca_pk, 0) == VBAN_E_CERT);         /* other CA */
    /* every single-bit change of the signed certificate is caught */
    for (i = 0; i < VBAN_CERT_SIZE * 8; ++i) {
        memcpy(enc, d.cert, VBAN_CERT_SIZE);
        enc[i / 8] ^= (uint8_t)(1u << (i % 8));
        if (!vban_cert_decode(enc, VBAN_CERT_SIZE, &c2) && !vban_cert_verify(&c2, p.ca_pk, 0))
            ++bad;
    }
    CHECK(bad == 0);
    /* format checks */
    memcpy(enc, d.cert, VBAN_CERT_SIZE);
    enc[64 + 10] = 'x';                       /* bytes after the name NUL must be zero */
    CHECK(vban_cert_decode(enc, VBAN_CERT_SIZE, &c2) == VBAN_E_FORMAT);
    memcpy(enc, d.cert, VBAN_CERT_SIZE);
    enc[64] = '\n';                           /* control character */
    CHECK(vban_cert_decode(enc, VBAN_CERT_SIZE, &c2) == VBAN_E_FORMAT);
    CHECK(vban_cert_decode(d.cert, VBAN_CERT_SIZE - 1, &c2) == VBAN_E_SIZE);
    strcpy(c2.name, "");
    CHECK(vban_cert_encode(&c2, enc) == VBAN_E_ARG);
    /* identity: certificate for another key, or not from this CA */
    {
        vban_sec_id_t id;
        uint8_t sk2[32], pk2[32];
        CHECK(!vban_sec_keygen(sk2, pk2));
        CHECK(vban_sec_id_init(&id, sk2, d.cert, VBAN_CERT_SIZE, p.ca_bytes, VBAN_CERT_SIZE) == VBAN_E_CERT);
        CHECK(vban_sec_id_init(&id, d.sk, d.cert, VBAN_CERT_SIZE, q.ca_bytes, VBAN_CERT_SIZE) == VBAN_E_CERT);
        CHECK(vban_sec_id_init(&id, d.sk, p.ca_bytes, VBAN_CERT_SIZE, p.ca_bytes, VBAN_CERT_SIZE) == VBAN_E_CERT);
    }
    /* requests */
    CHECK(!vban_cert_req_encode("Phone", VBAN_CERT_ROLE_RX, d.pk, req));
    CHECK(!vban_cert_req_decode(req, sizeof(req), name, &role, pk));
    CHECK(!strcmp(name, "Phone") && role == VBAN_CERT_ROLE_RX && !memcmp(pk, d.pk, 32));
    CHECK(vban_cert_req_encode("a name that is far too long for it", 1, d.pk, req) == VBAN_E_ARG);
    /* PEM */
    n = vban_pem_encode("VBAN CERT", d.cert, VBAN_CERT_SIZE, pem, sizeof(pem));
    CHECK(n > 0 && strstr(pem, "-----BEGIN VBAN CERT-----\n") == pem);
    CHECK(vban_pem_decode("VBAN CERT", pem, (size_t)n, bin, sizeof(bin)) == VBAN_CERT_SIZE);
    CHECK(!memcmp(bin, d.cert, VBAN_CERT_SIZE));
    CHECK(vban_pem_decode("VBAN REQUEST", pem, (size_t)n, bin, sizeof(bin)) == VBAN_E_NOTFOUND);
    CHECK(vban_pem_decode("VBAN CERT", pem, (size_t)n, bin, 10) == VBAN_E_NOSPACE);
    for (i = 1; i <= 5; ++i) {                /* all tail lengths round-trip */
        n = vban_pem_encode("X", d.cert, i, pem, sizeof(pem));
        CHECK(vban_pem_decode("X", pem, (size_t)n, bin, sizeof(bin)) == (int)i && !memcmp(bin, d.cert, i));
    }
    CHECK(vban_pem_decode("X", "-----BEGIN X-----\nQQ=A\n-----END X-----", 36, bin, sizeof(bin)) == VBAN_E_FORMAT);
    CHECK(vban_pem_decode("X", "-----BEGIN X-----\nQR==\n-----END X-----", 36, bin, sizeof(bin)) == VBAN_E_FORMAT);
    CHECK(vban_pem_encode("VBAN CERT", d.cert, VBAN_CERT_SIZE, pem, 100) == VBAN_E_NOSPACE);
}

/* ---- sessions ---- */
static int handshake(vban_sec_session_t *a, const vban_sec_id_t *ia, vban_sec_session_t *b, const vban_sec_id_t *ib,
                     uint64_t now)
{
    uint8_t m1[VBAN_SEC_HS_MAX], m2[VBAN_SEC_HS_MAX], m3[VBAN_SEC_HS_MAX], tmp[64];
    int n1, n2, n3, ev, r;
    n1 = vban_sec_connect(a, ia, 0x1111, now, m1, sizeof(m1));
    if (n1 <= 0) return -100;
    n2 = vban_sec_accept(b, ib, 0x2222, m1, (size_t)n1, now, m2, sizeof(m2));
    if (n2 <= 0) return n2 ? n2 : -101;
    n3 = vban_sec_handle(a, ia, m2, (size_t)n2, now, m3, sizeof(m3), &ev);
    if (n3 <= 0) return n3 ? n3 : -102;
    if (ev != VBAN_SEC_EV_SEND || a->state != VBAN_SEC_UP) return -103;
    r = vban_sec_handle(b, ib, m3, (size_t)n3, now, tmp, sizeof(tmp), &ev);
    if (r) return r;
    return ev == VBAN_SEC_EV_UP && b->state == VBAN_SEC_UP ? 0 : -104;
}

static int refuse_all(void *ctx, const vban_cert_t *peer)
{
    (void)peer;
    ++*(int *)ctx;
    return 1;
}

static void test_sessions(void)
{
    pki_t p, q;
    dev_t_ tx, hub, rogue, expired;
    vban_sec_session_t a, b;
    uint8_t inner[VBAN_SEC_INNER_MAX], pkt[VBAN_PACKET_MAX_SIZE], out[VBAN_PACKET_MAX_SIZE], keep[VBAN_PACKET_MAX_SIZE];
    int n, ev, i, calls = 0;

    pki_new(&p, "Studio CA");
    pki_new(&q, "Rogue CA");
    dev_new(&tx, &p, "PC mic", VBAN_CERT_ROLE_TX, 1000, 4000000000u);
    dev_new(&hub, &p, "Hub", VBAN_CERT_ROLE_HUB, 1000, 4000000000u);
    dev_new(&rogue, &q, "PC mic", VBAN_CERT_ROLE_TX, 1000, 4000000000u);
    dev_new(&expired, &p, "Old", VBAN_CERT_ROLE_TX, 1000, 2000);

    CHECK(handshake(&a, &tx.id, &b, &hub.id, 0) == 0);
    CHECK(!strcmp(a.peer.name, "Hub") && !strcmp(b.peer.name, "PC mic"));
    CHECK(a.remote_idx == 0x2222 && b.remote_idx == 0x1111);

    /* data both ways; the inner packet is a full VBAN packet */
    for (i = 0; i < (int)sizeof(inner); ++i)
        inner[i] = (uint8_t)(i * 7);
    n = vban_sec_seal(&a, inner, sizeof(inner), 1, pkt, sizeof(pkt));
    CHECK(n == VBAN_PACKET_MAX_SIZE);
    CHECK(pkt[4] == VBAN_PROTOCOL_USER && pkt[5] == VBAN_SEC_DATA);
    CHECK(vban_sec_handle(&b, &hub.id, pkt, (size_t)n, 1, out, sizeof(out), &ev) == (int)sizeof(inner));
    CHECK(ev == VBAN_SEC_EV_DATA && !memcmp(out, inner, sizeof(inner)));
    CHECK(vban_sec_handle(&b, &hub.id, pkt, (size_t)n, 1, out, sizeof(out), &ev) == VBAN_E_REPLAY);
    n = vban_sec_seal(&b, inner, 100, 1, pkt, sizeof(pkt));
    CHECK(vban_sec_handle(&a, &tx.id, pkt, (size_t)n, 1, out, sizeof(out), &ev) == 100 && !memcmp(out, inner, 100));
    CHECK(vban_sec_seal(&a, inner, VBAN_SEC_INNER_MAX + 1, 1, pkt, sizeof(pkt)) == VBAN_E_OVERSIZE);

    /* tampering: any bit of header, ciphertext or tag */
    n = vban_sec_seal(&a, inner, 60, 1, pkt, sizeof(pkt));
    {
        int caught = 0, total = 0;
        for (i = 0; i < n * 8; ++i) {
            int r;
            memcpy(keep, pkt, (size_t)n);
            keep[i / 8] ^= (uint8_t)(1u << (i % 8));
            r = vban_sec_handle(&b, &hub.id, keep, (size_t)n, 1, out, sizeof(out), &ev);
            ++total;
            if (r < 0)
                ++caught;
        }
        CHECK(caught == total);
    }
    CHECK(vban_sec_handle(&b, &hub.id, pkt, (size_t)n, 1, out, sizeof(out), &ev) == 60);  /* original still fine */

    /* reordering inside the window is fine, older than 64 is refused */
    {
        static uint8_t ring[70][128];
        int len[70];
        for (i = 0; i < 70; ++i)
            len[i] = vban_sec_seal(&a, inner, 80, 2, ring[i], sizeof(ring[i]));
        CHECK(vban_sec_handle(&b, &hub.id, ring[69], (size_t)len[69], 2, out, sizeof(out), &ev) == 80);
        CHECK(vban_sec_handle(&b, &hub.id, ring[10], (size_t)len[10], 2, out, sizeof(out), &ev) == 80);
        CHECK(vban_sec_handle(&b, &hub.id, ring[10], (size_t)len[10], 2, out, sizeof(out), &ev) == VBAN_E_REPLAY);
        CHECK(vban_sec_handle(&b, &hub.id, ring[4], (size_t)len[4], 2, out, sizeof(out), &ev) == VBAN_E_REPLAY);
        CHECK(vban_sec_handle(&b, &hub.id, ring[5], (size_t)len[5], 2, out, sizeof(out), &ev) == VBAN_E_REPLAY);
        CHECK(vban_sec_handle(&b, &hub.id, ring[6], (size_t)len[6], 2, out, sizeof(out), &ev) == 80);
    }
    /* keys expire: the initiator has to rekey */
    n = vban_sec_seal(&a, inner, 40, 2, pkt, sizeof(pkt));
    CHECK(vban_sec_handle(&b, &hub.id, pkt, (size_t)n, VBAN_SEC_EXPIRE_MS + 1, out, sizeof(out), &ev) == VBAN_E_STATE);
    CHECK(vban_sec_seal(&a, inner, 40, VBAN_SEC_EXPIRE_MS + 1, pkt, sizeof(pkt)) == VBAN_E_STATE);

    /* wrong CA, either side */
    CHECK(handshake(&a, &rogue.id, &b, &hub.id, 0) == VBAN_E_CERT);   /* rogue does not trust the hub */
    CHECK(a.state == VBAN_SEC_DEAD && b.state == VBAN_SEC_WAIT_HS3);
    {
        vban_sec_id_t r = rogue.id;                                    /* trusts our CA, cert from another */
        memcpy(r.ca_pk, p.ca_pk, 32);
        CHECK(handshake(&a, &r, &b, &hub.id, 0) == VBAN_E_CERT);      /* hub refuses at HS3 */
        CHECK(a.state == VBAN_SEC_UP && b.state == VBAN_SEC_DEAD);
    }
    CHECK(handshake(&a, &tx.id, &b, &rogue.id, 0) == VBAN_E_CERT);    /* device refuses a rogue hub */
    CHECK(a.state == VBAN_SEC_DEAD);
    /* expired certificate when the clock is known; accepted when it is not */
    hub.id.now_unix = 3000;
    CHECK(handshake(&a, &expired.id, &b, &hub.id, 0) == VBAN_E_CERT);
    hub.id.now_unix = 0;
    CHECK(handshake(&a, &expired.id, &b, &hub.id, 0) == 0);
    /* application refusal (revocation) */
    hub.id.authorize = refuse_all;
    hub.id.authorize_ctx = &calls;
    CHECK(handshake(&a, &tx.id, &b, &hub.id, 0) == VBAN_E_CERT && calls == 1);
    hub.id.authorize = NULL;
    /* a stolen certificate without its key: present tx's cert with rogue's key */
    {
        vban_sec_id_t thief = tx.id;
        memcpy(thief.sk, rogue.sk, 32);
        memcpy(thief.pk, rogue.pk, 32);
        CHECK(handshake(&a, &thief, &b, &hub.id, 0) == VBAN_E_CERT);
    }
    /* HS2 index in the outer header must match the authenticated one */
    {
        uint8_t m1[VBAN_SEC_HS_MAX], m2[VBAN_SEC_HS_MAX], m3[VBAN_SEC_HS_MAX];
        int n1 = vban_sec_connect(&a, &tx.id, 7, 0, m1, sizeof(m1));
        int n2 = vban_sec_accept(&b, &hub.id, 9, m1, (size_t)n1, 0, m2, sizeof(m2));
        m2[8] = 10;                            /* sender index 9 -> 10 */
        CHECK(vban_sec_handle(&a, &tx.id, m2, (size_t)n2, 0, m3, sizeof(m3), &ev) < 0);
        CHECK(a.state == VBAN_SEC_DEAD);
        /* an altered HS1 index breaks the transcript */
        n1 = vban_sec_connect(&a, &tx.id, 7, 0, m1, sizeof(m1));
        m1[8] = 8;
        n2 = vban_sec_accept(&b, &hub.id, 9, m1, (size_t)n1, 0, m2, sizeof(m2));
        CHECK(n2 > 0);
        m2[12] = 7;                            /* deliver it anyway */
        CHECK(vban_sec_handle(&a, &tx.id, m2, (size_t)n2, 0, m3, sizeof(m3), &ev) == VBAN_E_AUTH);
        CHECK(vban_sec_accept(&b, &hub.id, 9, m1, (size_t)n1 - 1, 0, m2, sizeof(m2)) == VBAN_E_FORMAT);
    }
}

/* ---- initiator link + responder table: retry, rekey, previous key ---- */
static void test_link_server(void)
{
    pki_t p;
    dev_t_ tx, hub;
    vban_sec_link_t l;
    vban_sec_peer_t slots[2];
    vban_sec_server_t srv;
    vban_sec_peer_t *peer;
    vban_addr_t from = { VBAN_IP(10, 0, 0, 2), 50000 };
    uint8_t hsbuf[VBAN_SEC_HS_MAX], reply[VBAN_SEC_HS_MAX], fin[VBAN_SEC_HS_MAX], tmp[64];
    uint8_t inner[200], pkt[400], out[400], old_pkt[400];
    uint64_t t = 1000;
    int n, ev, r, old_len;

    pki_new(&p, "CA");
    dev_new(&tx, &p, "Mic", VBAN_CERT_ROLE_TX, 1000, 4000000000u);
    dev_new(&hub, &p, "Hub", VBAN_CERT_ROLE_HUB, 1000, 4000000000u);
    memset(inner, 0x5A, sizeof(inner));
    vban_sec_link_init(&l);
    vban_sec_server_init(&srv, slots, 2, 20);

    CHECK(vban_sec_link_seal(&l, inner, 10, t, pkt, sizeof(pkt)) == VBAN_E_STATE);
    n = vban_sec_link_tick(&l, &tx.id, t, hsbuf, sizeof(hsbuf));
    CHECK(n == VBAN_SEC_HDR + 32);
    CHECK(vban_sec_link_tick(&l, &tx.id, t + 500, hsbuf, sizeof(hsbuf)) == 0);   /* waiting */
    /* HS1 lost: resent after 1 s with a new index */
    n = vban_sec_link_tick(&l, &tx.id, t + VBAN_SEC_RETRY_MS, hsbuf, sizeof(hsbuf));
    CHECK(n > 0);
    t += VBAN_SEC_RETRY_MS;
    r = vban_sec_server_handle(&srv, &hub.id, hsbuf, (size_t)n, &from, t, reply, sizeof(reply), &ev, &peer);
    CHECK(r > 0 && ev == VBAN_SEC_EV_SEND && peer);
    r = vban_sec_link_handle(&l, &tx.id, reply, (size_t)r, t, fin, sizeof(fin), &ev);
    CHECK(r > 0 && ev == VBAN_SEC_EV_SEND && vban_sec_link_up(&l, t));
    from.port = 50001;                         /* the device roams */
    r = vban_sec_server_handle(&srv, &hub.id, fin, (size_t)r, &from, t, tmp, sizeof(tmp), &ev, &peer);
    CHECK(r == 0 && ev == VBAN_SEC_EV_UP && peer && peer->addr.port == 50001);
    CHECK(!strcmp(peer->s.peer.name, "Mic"));

    n = vban_sec_link_seal(&l, inner, sizeof(inner), t, pkt, sizeof(pkt));
    r = vban_sec_server_handle(&srv, &hub.id, pkt, (size_t)n, &from, t, out, sizeof(out), &ev, &peer);
    CHECK(r == (int)sizeof(inner) && ev == VBAN_SEC_EV_DATA);
    /* hub -> device on the same session */
    n = vban_sec_seal(&peer->s, inner, 50, t, pkt, sizeof(pkt));
    CHECK(vban_sec_link_handle(&l, &tx.id, pkt, (size_t)n, t, out, sizeof(out), &ev) == 50 && ev == VBAN_SEC_EV_DATA);

    /* rekey after 120 s; a hub packet sealed with the old key still opens for 10 s */
    old_len = vban_sec_seal(&peer->s, inner, 60, t, old_pkt, sizeof(old_pkt));
    CHECK(vban_sec_link_tick(&l, &tx.id, t + 1000, hsbuf, sizeof(hsbuf)) == 0);
    t += VBAN_SEC_REKEY_MS;
    n = vban_sec_link_tick(&l, &tx.id, t, hsbuf, sizeof(hsbuf));
    CHECK(n > 0);
    CHECK(vban_sec_link_seal(&l, inner, 10, t, pkt, sizeof(pkt)) > 0);          /* old session still sends */
    r = vban_sec_server_handle(&srv, &hub.id, hsbuf, (size_t)n, &from, t, reply, sizeof(reply), &ev, &peer);
    CHECK(r > 0 && peer == &slots[1]);           /* a second slot; the first stays until it expires */
    r = vban_sec_link_handle(&l, &tx.id, reply, (size_t)r, t, fin, sizeof(fin), &ev);
    CHECK(r > 0 && ev == VBAN_SEC_EV_SEND);
    CHECK(vban_sec_server_handle(&srv, &hub.id, fin, (size_t)r, &from, t, tmp, sizeof(tmp), &ev, &peer) == 0);
    CHECK(vban_sec_link_handle(&l, &tx.id, old_pkt, (size_t)old_len, t + 5000, out, sizeof(out), &ev) == 60);
    CHECK(vban_sec_link_handle(&l, &tx.id, old_pkt, (size_t)old_len, t + 5000, out, sizeof(out), &ev) == VBAN_E_REPLAY);
    n = vban_sec_link_seal(&l, inner, 30, t, pkt, sizeof(pkt));
    r = vban_sec_server_handle(&srv, &hub.id, pkt, (size_t)n, &from, t, out, sizeof(out), &ev, &peer);
    CHECK(r == 30 && peer == &slots[1]);
    old_len = vban_sec_seal(&slots[0].s, inner, 60, t, old_pkt, sizeof(old_pkt));
    CHECK(vban_sec_link_tick(&l, &tx.id, t + VBAN_SEC_PREV_MS, hsbuf, sizeof(hsbuf)) == 0);  /* drops the old key */
    CHECK(vban_sec_link_handle(&l, &tx.id, old_pkt, (size_t)old_len, t + VBAN_SEC_PREV_MS, out, sizeof(out), &ev) < 0);

    /* full table and the handshake rate limit */
    {
        vban_sec_link_t l2;
        vban_sec_link_init(&l2);
        n = vban_sec_link_tick(&l2, &tx.id, t, hsbuf, sizeof(hsbuf));
        CHECK(vban_sec_server_handle(&srv, &hub.id, hsbuf, (size_t)n, &from, t, reply, sizeof(reply), &ev, &peer) ==
              VBAN_E_NOSPACE);         /* both slots live */
        vban_sec_server_init(&srv, slots, 2, 1);
        CHECK(vban_sec_server_handle(&srv, &hub.id, hsbuf, (size_t)n, &from, t, reply, sizeof(reply), &ev, &peer) > 0);
        CHECK(vban_sec_server_handle(&srv, &hub.id, hsbuf, (size_t)n, &from, t, reply, sizeof(reply), &ev, &peer) ==
              VBAN_E_NOSPACE && srv.hs_refused == 1);
    }
}

/* ---- end to end: vban_tx -> seal -> server -> vban_rx ---- */
typedef struct { vban_sec_link_t *l; const vban_sec_id_t *id; uint8_t wire[8][VBAN_PACKET_MAX_SIZE]; int len[8]; int n; } wrap_t;

static int sealing_send(void *ctx, const uint8_t *pkt, size_t len, const vban_addr_t *to)
{
    wrap_t *w = (wrap_t *)ctx;
    int n;
    (void)to;
    if (w->n >= 8)
        return VBAN_E_NOSPACE;
    n = vban_sec_link_seal(w->l, pkt, len, 5, w->wire[w->n], sizeof(w->wire[0]));
    if (n < 0)
        return n;
    w->len[w->n++] = n;
    return (int)len;
}

static int g_rx_samples;
static void on_packet(void *u, const vban_packet_t *p, const vban_addr_t *from)
{
    (void)u; (void)from;
    if (p->valid && !memcmp(p->name, "Secure1", 8))
        g_rx_samples += (int)p->samples;
}

static void test_end_to_end(void)
{
    pki_t p;
    dev_t_ tx, hub;
    vban_sec_link_t l;
    vban_sec_peer_t slots[1], *peer;
    vban_sec_server_t srv;
    vban_tx_t vtx;
    vban_tx_config_t cfg;
    vban_rx_t vrx;
    vban_rx_config_t rcfg;
    wrap_t w;
    vban_addr_t a = { VBAN_IP(127, 0, 0, 1), 6980 };
    uint8_t m[VBAN_SEC_HS_MAX], r2[VBAN_SEC_HS_MAX], r3[VBAN_SEC_HS_MAX], inner[VBAN_PACKET_MAX_SIZE];
    int16_t pcm[2 * 256];
    int n, ev, i;

    pki_new(&p, "CA");
    dev_new(&tx, &p, "Mic", VBAN_CERT_ROLE_TX, 1000, 4000000000u);
    dev_new(&hub, &p, "Hub", VBAN_CERT_ROLE_HUB, 1000, 4000000000u);
    vban_sec_link_init(&l);
    vban_sec_server_init(&srv, slots, 1, 20);
    n = vban_sec_link_tick(&l, &tx.id, 5, m, sizeof(m));
    n = vban_sec_server_handle(&srv, &hub.id, m, (size_t)n, &a, 5, r2, sizeof(r2), &ev, &peer);
    n = vban_sec_link_handle(&l, &tx.id, r2, (size_t)n, 5, r3, sizeof(r3), &ev);
    CHECK(vban_sec_server_handle(&srv, &hub.id, r3, (size_t)n, &a, 5, m, sizeof(m), &ev, &peer) == 0);

    memset(&w, 0, sizeof(w));
    w.l = &l;
    w.id = &tx.id;
    memset(&cfg, 0, sizeof(cfg));
    cfg.name = "Secure1";
    cfg.sample_rate = 48000;
    cfg.channels = 2;
    cfg.datatype = VBAN_DATATYPE_S16;
    cfg.frames_per_packet = 256;               /* 1024-byte payload: fits the 1392 limit */
    cfg.send = sealing_send;
    cfg.send_ctx = &w;
    CHECK(vban_tx_init(&vtx, &cfg) == VBAN_OK);
    CHECK(vban_tx_add_dest(&vtx, &a) == VBAN_OK);
    for (i = 0; i < 2 * 256; ++i)
        pcm[i] = (int16_t)(i * 50);
    CHECK(vban_tx_write_s16(&vtx, pcm, 256) >= 0);
    CHECK(vban_tx_write_s16(&vtx, pcm, 256) >= 0);
    CHECK(w.n == 2 && w.len[0] == VBAN_HEADER_SIZE + 1024 + VBAN_SEC_OVERHEAD);

    memset(&rcfg, 0, sizeof(rcfg));
    rcfg.on_unmatched = on_packet;
    vban_rx_init(&vrx, &rcfg);
    for (i = 0; i < w.n; ++i) {
        n = vban_sec_server_handle(&srv, &hub.id, w.wire[i], (size_t)w.len[i], &a, 5, inner, sizeof(inner), &ev, &peer);
        CHECK(ev == VBAN_SEC_EV_DATA && n == VBAN_HEADER_SIZE + 1024);
        if (n > 0)
            vban_rx_process(&vrx, inner, (size_t)n, &a, 5000000);
    }
    CHECK(g_rx_samples == 512);
}

int main(void)
{
    test_noise_vectors();
    test_certs();
    test_sessions();
    test_link_server();
    test_end_to_end();
    printf("%d checks, %d failed\n", g_checks, g_fail);
    return g_fail ? 1 : 0;
}
