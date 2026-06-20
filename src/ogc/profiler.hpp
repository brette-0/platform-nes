/**
 * @file profiler.hpp
 * @brief Public surface of the OGC on-target statistical PC sampler.
 *
 * The sampler (profiler.cpp) bins main-thread PCs into a histogram; the video
 * backend (video.cpp) reads the hottest entries via prof_top() each frame and
 * draws them as a hex+bar overlay. Resolve the on-screen hex addresses offline
 * with tools/profsym.sh against the linked demo.elf.
 */
#ifndef OGC_PROFILER_H
#define OGC_PROFILER_H

#include <intsh>
using namespace br0::intsh;

/** @brief One histogram entry: an instruction-aligned PC and its sample count. */
struct ProfEntry { u32 addr; u32 count; };

/** @brief Aggregate counters for the current sampling window. */
struct ProfStats {
    u32 total;     ///< counted samples (main thread RUNNING)
    u32 idle;      ///< skipped samples (main thread WAITING / blocked)
    u32 dropped;   ///< samples discarded because the histogram was full
};

/** @brief Starts the sampler thread. MUST be called on the main thread. */
void prof_init();

/** @brief Clears the histogram and counters (bind to a controller button). */
void prof_reset();

/**
 * @brief Copies the hottest @p max entries (descending by count) into @p out.
 * @param out   Caller array of at least @p max ProfEntry.
 * @param max   Capacity of @p out.
 * @param stats Optional; receives the window's aggregate counters.
 * @return Number of entries written (<= @p max).
 */
int prof_top(ProfEntry* out, int max, ProfStats* stats);

#endif // OGC_PROFILER_H
