/**
 * @file apu.hpp
 * @brief 2A03 APU write-only register file and PRESERVE/RESTORE family.
 *
 * Exposes every write-only APU register as a ::wo_register shadow so that
 * reads always return the last value written, and writes go to both the
 * shadow and the hardware port.
 *
 * The ::APU_REGISTERS family lets caller code bracket an ISR that touches
 * the APU with ::PRESERVE / ::RESTORE:
 *
 * @code
 *   PRESERVE(APU_REGISTERS);
 *   ScheduleInterrupt(loc, cycles, &ready);
 *   // ... APU is now free for timing use ...
 *
 *   // inside the IRQ handler:
 *   if (*ready) {
 *       RESTORE(APU_REGISTERS);
 *   }
 * @endcode
 *
 * Only available on NES builds: these addresses are hardware-mapped on the
 * 2A03 and meaningless on any other target.
 */
#ifndef APU_H
#define APU_H

#ifdef TARGET_NES

#include <platform-nes/technology.hpp>

/**
 * @brief All 2A03 APU write-only registers, grouped as a struct of shadows.
 *
 * Each field is a ::wo_register whose template argument is the physical
 * address of the corresponding APU port. Reads return the shadow (last
 * written value); writes update the shadow and poke the hardware port.
 * Addresses $4009 and $400D are unused by the 2A03 and are omitted.
 */
struct Apu {
    wo_register<0x4000> sq1_vol;       ///< Pulse 1 — duty / envelope / volume
    wo_register<0x4001> sq1_sweep;     ///< Pulse 1 — sweep unit
    wo_register<0x4002> sq1_lo;        ///< Pulse 1 — timer low 8 bits
    wo_register<0x4003> sq1_hi;        ///< Pulse 1 — length counter + timer high

    wo_register<0x4004> sq2_vol;       ///< Pulse 2 — duty / envelope / volume
    wo_register<0x4005> sq2_sweep;     ///< Pulse 2 — sweep unit
    wo_register<0x4006> sq2_lo;        ///< Pulse 2 — timer low 8 bits
    wo_register<0x4007> sq2_hi;        ///< Pulse 2 — length counter + timer high

    wo_register<0x4008> tri_linear;    ///< Triangle — linear counter control
    wo_register<0x400A> tri_lo;        ///< Triangle — timer low 8 bits
    wo_register<0x400B> tri_hi;        ///< Triangle — length counter + timer high

    wo_register<0x400C> noise_vol;     ///< Noise — envelope / volume
    wo_register<0x400E> noise_lo;      ///< Noise — mode + period
    wo_register<0x400F> noise_hi;      ///< Noise — length counter

    wo_register<0x4010> dmc_freq;      ///< DMC — IRQ enable, loop, rate index
    wo_register<0x4011> dmc_raw;       ///< DMC — direct load (7-bit DAC level)
    wo_register<0x4012> dmc_start;     ///< DMC — sample start address ($C000 base)
    wo_register<0x4013> dmc_len;       ///< DMC — sample byte count

    wo_register<0x4015> snd_chn;       ///< Channel enable / length counter status
    wo_register<0x4017> frame_counter; ///< Frame counter mode + IRQ inhibit
};

/** @brief The single global APU register file. */
extern Apu apu;

/**
 * @brief PRESERVE/RESTORE family covering all APU write-only registers.
 *
 * Expand this macro wherever ::PRESERVE / ::RESTORE / ::SHADOW expect a
 * comma-separated list of lvalues. It names every field of ::apu in the
 * same order as the struct definition.
 *
 * The companion `APU_REGISTERS_snapshot` array (defined in `src/nes/apu.cpp`,
 * declared `extern` below) is the flat byte storage that ::PRESERVE writes
 * into and ::RESTORE reads back from. Its size matches the number of
 * registers in this list exactly.
 *
 * @code
 *   PRESERVE(APU_REGISTERS);          // snapshot all 20 shadows, no poke
 *   ScheduleInterrupt(loc, cycles, &ready);
 *   // ... in the ISR:
 *   if (*ready) RESTORE(APU_REGISTERS); // replay shadows + poke hardware
 * @endcode
 */
#define APU_REGISTERS \
    apu.sq1_vol,   apu.sq1_sweep, apu.sq1_lo,  apu.sq1_hi,  \
    apu.sq2_vol,   apu.sq2_sweep, apu.sq2_lo,  apu.sq2_hi,  \
    apu.tri_linear,apu.tri_lo,    apu.tri_hi,               \
    apu.noise_vol, apu.noise_lo,  apu.noise_hi,              \
    apu.dmc_freq,  apu.dmc_raw,   apu.dmc_start,apu.dmc_len, \
    apu.snd_chn,   apu.frame_counter

/** @brief Flat snapshot storage for the ::APU_REGISTERS family (20 bytes). */
extern u8 APU_REGISTERS_snapshot[20];

#endif // TARGET_NES
#endif // APU_H
