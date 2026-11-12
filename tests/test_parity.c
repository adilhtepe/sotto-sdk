/*
 * Parity test: the snd-vban kernel receive ring (kernel/vban_kring.h, the exact
 * code the module runs) against the SDK jitter buffer (src/vban_jitter.c), on
 * the same simulated networks (bench/netsim.c) with the same buffering.
 *
 * The two implementations share a design (ADR-001) but not code (the kernel
 * one is integer-only), so this keeps them from drifting apart. Thresholds
 * come from measurement (README "Optimization log" 10); a regression in
 * either engine shows up as a gap between them.
 */
#define NETSIM_NO_MAIN
#include "../bench/netsim.c"

static int g_fail;
#define EXPECT(cond, ...) do { if (!(cond)) { ++g_fail; printf("  FAIL: " __VA_ARGS__); printf("\n"); } } while (0)

int main(void)
{
    const double secs = 40.0;
    const unsigned fpp = 64, period = 128;
    const char *fec_modes[2] = { "none", "dup:4" };
    unsigned s, m;

    printf("parity: SDK jitter buffer vs kernel ring, %.0f s per scenario\n", secs);
    printf("%-18s %-6s %9s %9s %9s %9s %9s %9s\n", "scenario", "fec", "sdk ms/m", "krn ms/m", "sdk jbms", "krn jbms",
           "re-prime", "skips");
    for (m = 0; m < 2; ++m) {
        g_fec_mode = m ? 1 : 0;
        g_fec_arg = m ? 4 : 0;
        for (s = 0; s < sizeof k_scen / sizeof k_scen[0]; ++s) {
            result_t a, b;
            g_rng = 0x9E3779B97F4A7C15ull + s;
            g_kring_target = 0;
            a = run(&k_scen[s], secs, fpp, period, 64);
            g_rng = 0x9E3779B97F4A7C15ull + s;   /* identical network */
            g_kring_target = (int)a.target;      /* same buffering the SDK settled on */
            b = run(&k_scen[s], secs, fpp, period, 64);
            printf("%-18s %-6s %9.1f %9.1f %9.2f %9.2f %9llu %9llu\n", k_scen[s].name, fec_modes[m],
                   a.glitch_ms_min, b.glitch_ms_min, a.jb_ms, b.jb_ms, (unsigned long long)b.kr_reprimes,
                   (unsigned long long)b.kr_skips);

            EXPECT(b.glitch_ms_min <= a.glitch_ms_min * 1.15 + 20.0,
                   "%s/%s: kernel glitch %.1f ms/min vs SDK %.1f", k_scen[s].name, fec_modes[m], b.glitch_ms_min, a.glitch_ms_min);
            /* The kernel may beat the SDK where it ends up buffering a bit more
             * (WAN: +1.3 ms, ~35 % fewer glitches), so the reverse bound is loose:
             * it only catches a kernel result that is too good to be real (e.g.
             * frames counted as played that were never placed). */
            EXPECT(a.glitch_ms_min <= b.glitch_ms_min * 2.0 + 40.0,
                   "%s/%s: SDK glitch %.1f ms/min vs kernel %.1f", k_scen[s].name, fec_modes[m], a.glitch_ms_min, b.glitch_ms_min);
            EXPECT(b.jb_ms <= a.jb_ms + 1.5,
                   "%s/%s: kernel buffers %.2f ms vs SDK %.2f", k_scen[s].name, fec_modes[m], b.jb_ms, a.jb_ms);
            EXPECT(b.kr_reprimes == 0, "%s/%s: kernel re-primed %llu times", k_scen[s].name, fec_modes[m],
                   (unsigned long long)b.kr_reprimes);
            EXPECT(b.kr_skips <= a.skips + 2, "%s/%s: kernel skipped %llu times vs SDK %u", k_scen[s].name, fec_modes[m],
                   (unsigned long long)b.kr_skips, a.skips);
            if (s == 0) {
                EXPECT(a.glitch_ms_min == 0.0 && b.glitch_ms_min == 0.0, "wired LAN must be glitch-free");
            }
        }
    }
    printf("%s\n", g_fail ? "PARITY FAILED" : "parity OK");
    return g_fail ? 1 : 0;
}
