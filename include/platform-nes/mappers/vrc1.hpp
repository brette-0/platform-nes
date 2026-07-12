#pragma once

#include <intsh>

#include <platform-nes/technology.hpp>

using namespace br0::intsh;

/*
 *  no long reads or long writes (not clean in c++) instead we favor longcalls and require get methods to be created
 *  I don't expect there to be much overhead to this, it will suggest encapsulation to the user
 */

/**
 * @brief Pins a function or variable into VRC1's fixed PRG-ROM bank ($E000-$FFFF).
 *
 * Backed by `.prg_rom_fixed`, the only segment vrc1.ld currently wires to a
 * real, always-mapped region -- see the file comment at the top of vrc1.ld.
 * Expands to nothing off-NES.
 */
#define fixed CREATE_SEGMENT_KEYWORD(".prg_rom_fixed")

/**
 * @brief VRC1's three switchable 8 KiB PRG-select registers ($8000/$A000/$C000).
 *
 * Not `const`: a "write-only register" that can never be written defeats the
 * point. Defined (not just declared) in vrc1.cpp, where the default-bank
 * boot-time init also lives.
 */
extern wo_register<0x8000> window1Control;
extern wo_register<0xa000> window2Control;
extern wo_register<0xc000> window3Control;

/**
 * @brief Writes @p ctx into a VRC1 PRG-select register, banking in that window.
 *
 * Thin, named wrapper over ::wo_register's write path (updates the RAM
 * shadow and pokes hardware together, so the two can never drift). @p reg is
 * taken by reference -- by value, the write would land on a throwaway copy:
 * the hardware poke would still happen (the address is a template constant,
 * not tied to which copy calls it), but the shadow update would be lost,
 * leaving the original register's tracked state stale.
 *
 * Not used by ::_reset (vrc1.cpp): that runs before .bss has been zeroed,
 * so window1Control/2/3's shadows aren't valid to write yet regardless of
 * how this compiles -- see ::_reset's own comment for why it pokes hardware
 * directly instead.
 */
template <u16 addr>
constexpr void SwitchBank(wo_register<addr> &reg, u8 ctx) {
    reg = ctx;
}

namespace vrc1_detail {
    /**
     * @brief Implementation of ::Long once the target register is resolved.
     *
     * Not for direct use -- call ::Long. Saves @p reg (via ::SHADOW, which
     * restores on any exit path, including a `return` straight out of the
     * body below), switches it to @p bank, invokes @p fn, and writes the
     * prior value back as the scope closes.
     */
    template <typename TReturn, u16 addr, typename TFunc>
    fixed TReturn CallInWindow(wo_register<addr> &reg, u8 bank, TFunc fn) {
        SHADOW(reg) {
            SwitchBank(reg, bank);
            return fn();
        }
        __builtin_unreachable(); // SHADOW's scope always runs its body
                                 // exactly once and returns; the compiler
                                 // can't see that through the
                                 // single-iteration for loop.
    }
} // namespace vrc1_detail

/**
 * @brief Calls @p fn with one of VRC1's three switchable windows temporarily
 *        re-banked, then restores whatever was banked there before returning.
 *
 * @p window selects *which* window gets switched -- 0 for window1Control
 * ($8000-$9FFF), 1 for window2Control ($A000-$BFFF), 2 for window3Control
 * ($C000-$DFFF) -- defaulting to window 0 when omitted. Lives in the fixed
 * bank (::fixed) so it's always reachable regardless of what's currently
 * banked in elsewhere.
 *
 * @p fn may be any callable -- a captureless function pointer or a lambda
 * (captures included, for passing arguments) -- so long as `fn()` is valid
 * and convertible to @p TReturn.
 *
 * @note At the project's current 32 KiB PRG-ROM there is exactly one valid
 *       bank per window, so the bank written here is that window's own
 *       boot-time default (see vrc1.cpp) -- today this call reasserts the
 *       window's already-active bank rather than switching to a different
 *       one. Once real multi-bank content exists per window (the
 *       `.prg_rom_0` / `.prg_rom_1` / `.prg_rom_2` tagging vrc1.ld's file
 *       comment calls out as future work), an explicit target-bank
 *       parameter will need to be added here.
 *
 * @tparam TReturn Result type of the call; not deducible from @p fn, so
 *                 specify it explicitly at the call site, e.g. `Long<int>(...)`.
 * @param fn     Callable to invoke once the selected window is banked in.
 * @param window Which window to switch: 0, 1, or 2 (default 0).
 */
template <typename TReturn, typename TFunc>
fixed TReturn Long(TFunc fn, const u8 window = 0) {
    switch (window) {
        case 1:  return vrc1_detail::CallInWindow<TReturn>(window2Control, 1, fn);
        case 2:  return vrc1_detail::CallInWindow<TReturn>(window3Control, 2, fn);
        default: return vrc1_detail::CallInWindow<TReturn>(window1Control, 0, fn);
    }
}

/*
 * CHR bankswitching. VRC1 has two independent 4 KiB CHR windows -- PPU
 * $0000-$0FFF ("pattern table 0") and $1000-$1FFF ("pattern table 1"),
 * matching this project's own terminology in video.hpp (::PatternTables,
 * ::CHR_TILES_PER_TABLE) -- each showing one of up to 32 physical 4 KiB
 * banks (128 KiB CHR-ROM max). Each bank number is 5 bits: 4 low bits
 * written to that window's own select register ($E000 / $F000), plus 1
 * high bit packed into a register the two windows *share* ($9000: bit 0
 * for pattern table 0, bit 1 for pattern table 1) -- so setting one
 * window's high bit is a read-modify-write against the other window's
 * current bit, not a plain overwrite.
 */

/**
 * @brief VRC1's CHR-select registers ($9000/$E000/$F000).
 *
 * chrHighBits ($9000) is shared between both pattern tables (bit 0 = table
 * 0's bank bit 4, bit 1 = table 1's), which is exactly what ::wo_register's
 * shadow exists for: ::SwitchCHRBank reads it back to flip only the calling
 * window's own bit, never the other window's. chr0Control ($E000) and
 * chr1Control ($F000) hold each table's low 4 bank bits. Not `const`,
 * same reasoning as window1Control etc.
 */
extern wo_register<0x9000> chrHighBits;
extern wo_register<0xe000> chr0Control;
extern wo_register<0xf000> chr1Control;

/**
 * @brief Selects which 4 KiB CHR-ROM bank appears in a VRC1 pattern table.
 *
 * Single template, mirroring ::SwitchBank's shape: @p addr (deduced from
 * @p reg) picks between chr0Control ($E000, PPU $0000-$0FFF / pattern table
 * 0) and chr1Control ($F000, PPU $1000-$1FFF / pattern table 1), which in
 * turn selects which bit of the *shared* chrHighBits register this call is
 * allowed to touch -- see the comment above chrHighBits. The read-modify-
 * write against chrHighBits' shadow is what keeps the other window's high
 * bit untouched.
 *
 * @param reg  chr0Control or chr1Control; any other instantiation is a
 *             compile error.
 * @param bank Target bank, 0-31.
 */
template <u16 addr>
constexpr void SwitchCHRBank(wo_register<addr> &reg, u8 bank) {
    static_assert(addr == 0xe000 || addr == 0xf000,
                  "SwitchCHRBank only valid for chr0Control ($E000) or chr1Control ($F000)");
    constexpr u8 bit = (addr == 0xe000) ? 0 : 1;
    reg = bank & 0x0F;
    chrHighBits = (chrHighBits.get() & ~(u8(1) << bit)) | (((bank >> 4) & 1) << bit);
}