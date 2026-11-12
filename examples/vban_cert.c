/*
 * vban_cert - keys and certificates for secure VBAN.
 *
 *   vban_cert ca     <name> <days> <ca.key> <ca.crt>      new network CA
 *   vban_cert key    <device.key>                         new device key (stays on the device)
 *   vban_cert req    <device.key> <name> <role> <out.req> request to send to the CA
 *   vban_cert sign   <ca.key> <ca.crt> <in.req> <days> <out.crt>
 *   vban_cert show   <file>                               print a certificate or request
 *   vban_cert verify <ca.crt> <device.crt>
 *
 *   role: tx | rx | txrx | hub
 *
 * Key files are created with mode 0600 and never overwritten.
 */
#include "common.h"
#include <errno.h>
#ifndef _WIN32
#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>
#endif

static int write_new(const char *path, const char *text, int secret)
{
    FILE *f;
#ifndef _WIN32
    int fd = open(path, O_WRONLY | O_CREAT | O_EXCL, secret ? 0600 : 0644);
    if (fd < 0) {
        fprintf(stderr, "%s: %s%s\n", path, strerror(errno), errno == EEXIST ? " (not overwriting)" : "");
        return -1;
    }
    f = fdopen(fd, "w");
#else
    (void)secret;
    if ((f = fopen(path, "r")) != NULL) {
        fclose(f);
        fprintf(stderr, "%s: exists (not overwriting)\n", path);
        return -1;
    }
    f = fopen(path, "w");
#endif
    if (!f || fputs(text, f) < 0 || fclose(f)) {
        fprintf(stderr, "%s: write failed\n", path);
        return -1;
    }
    return 0;
}

static int save(const char *path, const char *label, const uint8_t *bin, size_t len, int secret)
{
    char pem[1024];
    int r;
    if (vban_pem_encode(label, bin, len, pem, sizeof pem) < 0)
        return -1;
    r = write_new(path, pem, secret);
    memset(pem, 0, sizeof pem);
    return r;
}

static int parse_role(const char *s)
{
    if (!strcmp(s, "tx")) return VBAN_CERT_ROLE_TX;
    if (!strcmp(s, "rx")) return VBAN_CERT_ROLE_RX;
    if (!strcmp(s, "txrx")) return VBAN_CERT_ROLE_TX | VBAN_CERT_ROLE_RX;
    if (!strcmp(s, "hub")) return VBAN_CERT_ROLE_HUB;
    return -1;
}

static const char *role_text(unsigned r)
{
    static char b[32];
    b[0] = 0;
    if (r & VBAN_CERT_ROLE_CA) strcat(b, "ca ");
    if (r & VBAN_CERT_ROLE_HUB) strcat(b, "hub ");
    if (r & VBAN_CERT_ROLE_TX) strcat(b, "tx ");
    if (r & VBAN_CERT_ROLE_RX) strcat(b, "rx ");
    if (b[0]) b[strlen(b) - 1] = 0;
    return b;
}

static void hex(const uint8_t *p, size_t n)
{
    size_t i;
    for (i = 0; i < n; ++i)
        printf("%02x", p[i]);
}

static void print_time(uint64_t t)
{
    time_t tt = (time_t)t;
    struct tm *tm = gmtime(&tt);
    char b[32];
    if (t > 32503680000ull || !tm || !strftime(b, sizeof b, "%Y-%m-%d %H:%M:%S UTC", tm))
        printf("%llu", (unsigned long long)t);
    else
        printf("%s", b);
}

static void print_cert(const vban_cert_t *c)
{
    char name[VBAN_CERT_NAME_MAX * 2 + 1];
    printf("certificate\n  name:   %s\n  role:   %s\n  serial: %llu\n  valid:  ", ex_safe_text(c->name, sizeof c->name,
           name, sizeof name), role_text(c->role), (unsigned long long)c->serial);
    print_time(c->not_before);
    printf(" .. ");
    print_time(c->not_after);
    printf("\n  key:    ");
    hex(c->key, 32);
    printf("\n  issuer: ");
    hex(c->issuer, 8);
    printf("\n");
}

static uint64_t random_serial(void)
{
    uint8_t b[8];
    uint64_t v = 0;
    int i;
    if (vban_sec_random(b, 8))
        return 1;
    for (i = 0; i < 8; ++i)
        v = (v << 8) | b[i];
    return v >> 1 ? v >> 1 : 1;           /* 63 bits, non-zero */
}

static int load_cert(const char *path, vban_cert_t *c, uint8_t bin[VBAN_CERT_SIZE])
{
    uint8_t tmp[VBAN_CERT_SIZE];
    if (ex_load_pem(path, "VBAN CERT", bin ? bin : tmp, VBAN_CERT_SIZE))
        return -1;
    if (vban_cert_decode(bin ? bin : tmp, VBAN_CERT_SIZE, c)) {
        fprintf(stderr, "%s: malformed certificate\n", path);
        return -1;
    }
    return 0;
}

int main(int argc, char **argv)
{
    const char *cmd = argc > 1 ? argv[1] : "";
    (void)ex_init;   /* no network needed */
    uint64_t now = (uint64_t)time(NULL);

    if (!strcmp(cmd, "ca") && argc == 6) {
        uint8_t sk[64], pk[32], bin[VBAN_CERT_SIZE];
        vban_cert_t c;
        long days = atol(argv[3]);
        if (days < 1 || vban_ca_keygen(sk, pk) ||
            vban_ca_cert(&c, argv[2], sk, pk, now - 3600, now + (uint64_t)days * 86400u, random_serial()) ||
            vban_cert_encode(&c, bin)) {
            fprintf(stderr, "bad name (1..32 printable chars) or days, or no random source\n");
            return 2;
        }
        if (save(argv[4], "VBAN CA KEY", sk, sizeof sk, 1) || save(argv[5], "VBAN CERT", bin, sizeof bin, 0))
            return 1;
        memset(sk, 0, sizeof sk);
        print_cert(&c);
        return 0;
    }
    if (!strcmp(cmd, "key") && argc == 3) {
        uint8_t sk[32], pk[32];
        if (vban_sec_keygen(sk, pk)) {
            fprintf(stderr, "no random source\n");
            return 1;
        }
        if (save(argv[2], "VBAN PRIVATE KEY", sk, sizeof sk, 1))
            return 1;
        memset(sk, 0, sizeof sk);
        printf("public key: ");
        hex(pk, 32);
        printf("\n");
        return 0;
    }
    if (!strcmp(cmd, "req") && argc == 6) {
        uint8_t sk[32], pk[32], req[VBAN_CERT_REQ_SIZE];
        int role = parse_role(argv[4]);
        if (role < 0) {
            fprintf(stderr, "role: tx | rx | txrx | hub\n");
            return 2;
        }
        if (ex_load_pem(argv[2], "VBAN PRIVATE KEY", sk, sizeof sk))
            return 1;
        vban_sec_pubkey(sk, pk);
        memset(sk, 0, sizeof sk);
        if (vban_cert_req_encode(argv[3], (uint8_t)role, pk, req)) {
            fprintf(stderr, "name: 1..32 printable characters\n");
            return 2;
        }
        return save(argv[5], "VBAN REQUEST", req, sizeof req, 0) ? 1 : 0;
    }
    if (!strcmp(cmd, "sign") && argc == 7) {
        uint8_t sk[64], pk[32], req[VBAN_CERT_REQ_SIZE], bin[VBAN_CERT_SIZE];
        vban_cert_t ca, c;
        long days = atol(argv[5]);
        if (ex_load_pem(argv[2], "VBAN CA KEY", sk, sizeof sk) || load_cert(argv[3], &ca, NULL) ||
            ex_load_pem(argv[4], "VBAN REQUEST", req, sizeof req))
            return 1;
        /* the CA key must belong to the CA certificate */
        {   /* sign a throw-away certificate and check it with the CA certificate's key */
            vban_cert_t probe = ca;
            if (vban_cert_verify(&ca, ca.key, 0) || !(ca.role & VBAN_CERT_ROLE_CA) ||
                vban_cert_sign(&probe, sk, ca.key) || vban_cert_verify(&probe, ca.key, 0)) {
                fprintf(stderr, "the CA key does not match %s\n", argv[3]);
                return 1;
            }
            memcpy(pk, ca.key, 32);
        }
        memset(&c, 0, sizeof c);
        if (days < 1 || vban_cert_req_decode(req, sizeof req, c.name, &c.role, c.key)) {
            fprintf(stderr, "malformed request or bad days\n");
            return 1;
        }
        if (c.role & VBAN_CERT_ROLE_CA) {
            fprintf(stderr, "refusing to sign a CA role\n");
            return 1;
        }
        c.not_before = now - 3600;
        c.not_after = now + (uint64_t)days * 86400u;
        if (c.not_after > ca.not_after)
            c.not_after = ca.not_after;       /* never outlive the CA */
        c.serial = random_serial();
        if (vban_cert_sign(&c, sk, pk) || vban_cert_encode(&c, bin))
            return 1;
        memset(sk, 0, sizeof sk);
        if (save(argv[6], "VBAN CERT", bin, sizeof bin, 0))
            return 1;
        print_cert(&c);
        return 0;
    }
    if (!strcmp(cmd, "show") && argc == 3) {
        char txt[4096];
        uint8_t bin[VBAN_CERT_SIZE];
        int n = ex_read_file(argv[2], txt, sizeof txt);
        vban_cert_t c;
        if (n < 0) {
            fprintf(stderr, "cannot read %s\n", argv[2]);
            return 1;
        }
        if (vban_pem_decode("VBAN CERT", txt, (size_t)n, bin, sizeof bin) == VBAN_CERT_SIZE &&
            !vban_cert_decode(bin, sizeof bin, &c)) {
            print_cert(&c);
            if (c.role & VBAN_CERT_ROLE_CA)
                printf("  self-signature: %s\n", vban_cert_verify(&c, c.key, 0) ? "INVALID" : "ok");
            return 0;
        }
        if (vban_pem_decode("VBAN REQUEST", txt, (size_t)n, bin, sizeof bin) == VBAN_CERT_REQ_SIZE) {
            char name[VBAN_CERT_NAME_MAX + 1], safe[VBAN_CERT_NAME_MAX * 2 + 1];
            uint8_t role, pk[32];
            if (!vban_cert_req_decode(bin, VBAN_CERT_REQ_SIZE, name, &role, pk)) {
                printf("request\n  name: %s\n  role: %s\n  key:  ", ex_safe_text(name, sizeof name, safe, sizeof safe),
                       role_text(role));
                hex(pk, 32);
                printf("\n");
                return 0;
            }
        }
        fprintf(stderr, "%s: no certificate or request\n", argv[2]);
        return 1;
    }
    if (!strcmp(cmd, "verify") && argc == 4) {
        vban_cert_t ca, c;
        int r;
        if (load_cert(argv[2], &ca, NULL) || load_cert(argv[3], &c, NULL))
            return 1;
        if (!(ca.role & VBAN_CERT_ROLE_CA) || vban_cert_verify(&ca, ca.key, now)) {
            fprintf(stderr, "%s is not a valid CA certificate\n", argv[2]);
            return 1;
        }
        r = vban_cert_verify(&c, ca.key, now);
        printf("%s\n", r ? "INVALID (wrong CA, tampered, or outside its validity)" : "ok");
        return r ? 1 : 0;
    }
    fprintf(stderr,
            "usage:\n"
            "  vban_cert ca     <name> <days> <ca.key> <ca.crt>\n"
            "  vban_cert key    <device.key>\n"
            "  vban_cert req    <device.key> <name> <tx|rx|txrx|hub> <out.req>\n"
            "  vban_cert sign   <ca.key> <ca.crt> <in.req> <days> <out.crt>\n"
            "  vban_cert show   <file>\n"
            "  vban_cert verify <ca.crt> <device.crt>\n");
    return 2;
}
