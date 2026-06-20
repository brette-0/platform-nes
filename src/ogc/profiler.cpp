/**
 * @file profiler.cpp
 * @brief On-target statistical PC sampler for the libogc (GameCube + Wii) backend.
 *
 * Dolphin reports the emulator at 100% speed with huge headroom, yet the game
 * still hitches -- which means the cost is episodic CPU work somewhere in the
 * per-frame game logic, not a graphics or throughput problem. To find *where*
 * (including code we haven't manually bracketed), this samples the program
 * counter of the main thread at ~10 kHz and bins the hits into a histogram. The
 * hottest addresses are read back by the video backend and drawn as a hex+bar
 * overlay; resolve the hex offline against demo.elf with tools/profsym.sh.
 *
 * Mechanism (modern libogc is built on the `tuxedo` microkernel):
 *   - Every thread's saved CPU context lives at the head of its TCB
 *     (`KThread::ctx`, a `PPCContext` whose first word is the interrupted PC).
 *   - We capture the main thread's `KThread*` at init (on the main thread), then
 *     run a higher-priority sampler thread that sleeps ~100us, wakes (preempting
 *     main, so the scheduler has just saved main's PC into `ctx.pc`), reads that
 *     PC and records it.
 *   - We only count a sample when main is `KTHR_STATE_RUNNING` (runnable). While
 *     it is `KTHR_STATE_WAITING` (blocked in VIDEO_WaitVSync) its saved PC is
 *     stale idle time, which we skip -- so the histogram reflects actual on-CPU
 *     work, not the frame's spare headroom.
 *
 * This touches no exception vectors and uses only public tuxedo structs, so it
 * cannot destabilise libogc's own timing/threading. It is OGC-only and never
 * compiled into the NES target.
 */
#include "internal.hpp"
#include "profiler.hpp"

// Development tool only: compiled into Debug .dols (gc-debug/wii-debug), where
// CMake defines OGC_PROFILER. Release builds get an empty translation unit, so
// the sampler thread and histogram never ship. The matching call sites in
// video.cpp are gated the same way; profiler.hpp declares the API unconditionally
// (a header has no config knowledge), but with OGC_PROFILER off nothing calls it.
#if defined(OGC_PROFILER)

#include <tuxedo/thread.h>
#include <malloc.h>
#include <string.h>

namespace {

// Open-addressed histogram: power-of-two slots, linear probe, drop-on-full.
// Hot addresses are sampled early and often, so they land before the table
// fills; only cold late-comers are dropped, which is fine since we show top-N.
constexpr int   HASH_SLOTS = 8192;
constexpr u32   SAMPLE_US  = 100;          // ~10 kHz sampling

struct Slot { u32 addr; u32 count; };      // addr == 0 marks an empty slot

Slot*    g_hist   = nullptr;
u32      g_total  = 0;                      // counted (RUNNING) samples
u32      g_idle   = 0;                      // skipped (WAITING) samples
u32      g_drop   = 0;                      // samples that found the table full
KThread* g_main   = nullptr;

KThread        g_sampler;
alignas(16) u8 g_sampler_stack[8192];

void record(u32 pc) {
    pc &= ~3u;                             // instruction-align
    if (!pc) return;
    u32 i = (pc >> 2) & (HASH_SLOTS - 1);
    for (int probe = 0; probe < HASH_SLOTS; probe++) {
        Slot& s = g_hist[i];
        if (s.addr == pc)  { s.count++; return; }
        if (s.addr == 0)   { s.addr = pc; s.count = 1; return; }
        i = (i + 1) & (HASH_SLOTS - 1);
    }
    g_drop++;
}

sptr sampler_fn(void*) {
    for (;;) {
        KThreadSleepUs(SAMPLE_US);
        // Only main's *runnable* time is real work; its blocked-on-VSync time
        // would otherwise dominate (we have ~10x headroom) and bury the hotspots.
        if (g_main->state == KTHR_STATE_RUNNING) {
            g_total++;
            record(g_main->ctx.pc);
        } else {
            g_idle++;
        }
    }
    return 0;
}

}   // namespace

void prof_init() {
    g_main = KThreadGetSelf();             // must be called on the main thread
    g_hist = static_cast<Slot*>(memalign(32, HASH_SLOTS * sizeof(Slot)));
    prof_reset();
    // Priority below main's 0x3f (numerically lower == higher priority) so the
    // sampler preempts the game logic; its per-tick work is negligible.
    KThreadPrepare(&g_sampler, sampler_fn, nullptr,
                   g_sampler_stack + sizeof(g_sampler_stack), 0x10);
    KThreadResume(&g_sampler);
}

void prof_reset() {
    // Lock-free clear: a sample racing the wipe is at worst miscounted once,
    // which is harmless and avoids perturbing the timing we are measuring.
    memset(g_hist, 0, HASH_SLOTS * sizeof(Slot));
    g_total = g_idle = g_drop = 0;
}

int prof_top(ProfEntry* out, int max, ProfStats* stats) {
    if (stats) { stats->total = g_total; stats->idle = g_idle; stats->dropped = g_drop; }
    int n = 0;
    for (int k = 0; k < max; k++) {
        u32 best = 0;
        int bi   = -1;
        for (int i = 0; i < HASH_SLOTS; i++) {
            if (g_hist[i].addr == 0 || g_hist[i].count <= best) continue;
            bool taken = false;
            for (int j = 0; j < n; j++)
                if (out[j].addr == g_hist[i].addr) { taken = true; break; }
            if (taken) continue;
            best = g_hist[i].count;
            bi   = i;
        }
        if (bi < 0) break;
        out[n].addr  = g_hist[bi].addr;
        out[n].count = g_hist[bi].count;
        n++;
    }
    return n;
}

#endif // OGC_PROFILER
