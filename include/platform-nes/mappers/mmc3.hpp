/**
 * @file mmc3.hpp
 * @brief MMC3 mapper (mapper 4): PRG/CHR bank switching, scanline IRQ, and
 *        the ::fixed / ::cartmem / ::sysmem segment keywords.
 *
 * MMC3 hardware, as this module uses it (PRG mode 0, CHR mode 0 -- the only
 * combination this module supports; see mmc3.cpp's ::_reset):
 *
 *   $8000-$9FFF  8 KiB PRG-ROM, switchable (register R6, via $8000/$8001)
 *   $A000-$BFFF  8 KiB PRG-ROM, switchable (register R7, via $8000/$8001)
 *   $C000-$DFFF  8 KiB PRG-ROM, FIXED to the second-to-last physical bank
 *   $E000-$FFFF  8 KiB PRG-ROM, FIXED to the last physical bank
 *   $6000-$7FFF  up to 8 KiB PRG-RAM ("cartridge memory", see ::cartmem)
 *   $0000-$07FF  CHR-ROM, six windows: two 2 KiB (R0/R1), four 1 KiB (R2-R5)
 *
 * UNLIKE VRC1's three independently-selectable PRG windows, MMC3 in PRG
 * mode 0 only exposes TWO registers a project can point anywhere: R6
 * ($8000) and R7 ($A000). $C000-$DFFF is hardware-fixed to the second-to-
 * last bank regardless of any register write -- there is no third
 * switchable window, and this file never declares a "window3Control": no
 * register backs one. Its content also isn't stable across ROM growth (it
 * always shows whatever is CURRENTLY second-to-last, which shifts as more
 * banks are added), so unlike ::fixed, nothing should target it through the
 * farcall machinery below -- see ::detail::CallInSection's own comment.
 *
 * Every register on this chip except $A000 (mirroring) and $A001 (PRG-RAM
 * protect) is reached through the SAME two physical addresses ($8000
 * select, $8001 data) -- see ::mmc3_register, the indexed analogue of
 * VRC1's ::wo_register.
 */
#pragma once

#include <intsh>
#include <utility>

#include <platform-nes/technology.hpp>

using namespace br0::intsh;

/**
 * @brief Pins a function or variable into MMC3's fixed PRG-ROM bank
 *        ($E000-$FFFF).
 *
 * Same name and section as VRC1's ::fixed (mmc3-helper.ld routes
 * `.prg_rom_fixed` to the identical $E000-$FFFF address range VRC1 does),
 * since both chips hardwire that window the same way. Expands to nothing
 * off-NES.
 */
#define fixed CREATE_SEGMENT_KEYWORD(".prg_rom_fixed")

/**
 * @brief Pins a variable into MMC3's PRG-RAM ("cartridge memory",
 *        $6000-$7FFF).
 *
 * Explicit placement, the same way ::fixed pins code into $E000-$FFFF: use
 * for state that should live in cartridge RAM specifically (e.g. a battery-
 * backed save struct) rather than wherever ordinary .bss/.data happens to
 * land. Backed by `.cartmem`, NOLOAD (mmc3-helper.ld) -- runtime storage
 * only, nothing is loaded into it from the ROM file. Expands to nothing
 * off-NES.
 */
#define cartmem CREATE_SEGMENT_KEYWORD(".cartmem")

/**
 * @brief Pins a variable into the NES's own onboard system RAM, explicitly.
 *
 * Ordinary globals already land in system RAM via .bss/.data's default
 * placement; ::sysmem exists for state that specifically must NOT drift
 * into ::cartmem or any other region by accident -- an explicit statement
 * of intent, not a different physical destination than the untagged
 * default. Backed by `.sysmem`, NOLOAD (mmc3-helper.ld). Expands to nothing
 * off-NES.
 */
#define sysmem CREATE_SEGMENT_KEYWORD(".sysmem")

namespace mmc3 {

namespace detail {
    /**
     * @brief Bit 6 (PRG mode) / bit 7 (CHR A12 invert) of every $8000 write.
     *
     * $8000 packs the register-select index (bits 0-2) together with these
     * two latched mode bits in the SAME byte, so selecting a different
     * index without re-asserting the last mode bits would silently clear
     * whichever one wasn't also specified again. This module fixes both
     * bits at 0 permanently (PRG mode 0, CHR non-inverted -- see this
     * file's header comment) and never exposes a way to change them, so
     * every $8000 write below is simply the bare register index; there is
     * no runtime state to track. If a future revision needs to flip either
     * bit, THIS is the one place that assumption lives.
     */
    inline constexpr u8 kModeBits = 0;
} // namespace detail

/**
 * @brief One of MMC3's eight indexed bank registers (R0-R7), reached
 *        through the shared $8000 (select) / $8001 (data) port pair.
 *
 * Unlike ::wo_register (one dedicated hardware address per instance), every
 * mmc3_register<Index> shares the SAME two physical addresses -- what makes
 * an instance distinct is which index gets latched into $8000 immediately
 * before the $8001 write. Real hardware has eight independent internal
 * latches behind that one port pair, so each instance still owns its own
 * RAM shadow, and get()/::SHADOW/save-restore all work exactly like
 * ::wo_register's.
 *
 * @tparam Index Register select value, 0-7 (R0-R7).
 */
template <u8 Index>
class mmc3_register {
    atomic u8 shadow_;
public:
    static_assert(Index <= 7, "MMC3 only has registers R0-R7.");

    mmc3_register() = default;                                        ///< trivial: instance lives in .bss
    mmc3_register(const mmc3_register &o) : shadow_(o.shadow_) {}     ///< snapshot, no poke

    u8   get() const     { return shadow_; }                          ///< read  = shadow
    void set(const u8 v) { shadow_ = v; poke_only(v); }               ///< write = shadow + hardware
    void poke_only(const u8 v) const {                                 ///< write hardware only, no shadow update
        tech::poke(0x8000, detail::kModeBits | Index);
        tech::poke(0x8001, v);
    }

    operator u8() const { return shadow_; }
    mmc3_register &operator=(const u8 v)               { set(v);         return *this; }
    mmc3_register &operator=(const mmc3_register &o)   { set(o.shadow_); return *this; } ///< restore path
};

/**
 * @brief MMC3's two register-controlled PRG-select windows.
 *
 * Not `const`: same reasoning as VRC1's window1Control etc. -- a write-only
 * register that can never be written defeats the point. Defined (not just
 * declared) in mmc3.cpp, where the default-bank boot-time init also lives.
 * There is no window3Control -- see this file's header comment.
 */
extern mmc3_register<6> window1Control; ///< PRG-select R6, window 1 ($8000-$9FFF). See above.
extern mmc3_register<7> window2Control; ///< PRG-select R7, window 2 ($A000-$BFFF). See ::window1Control.

/**
 * @brief MMC3's six CHR-select registers (R0-R5).
 *
 * chr0Control/chr1Control select 2 KiB banks (PPU $0000/$0800 under CHR
 * mode 0); the low bit of the written value is ignored by hardware at that
 * granularity, though nothing here masks it away -- the shadow simply holds
 * whatever was written. chr2Control-chr5Control select 1 KiB banks (PPU
 * $1000/$1400/$1800/$1C00), full 8-bit range (up to 256 KiB CHR-ROM). Named
 * chrNControl to match VRC1's chr0Control/chr1Control convention as closely
 * as this chip's extra granularity allows.
 */
extern mmc3_register<0> chr0Control; ///< CHR-select R0, 2 KiB @ PPU $0000.
extern mmc3_register<1> chr1Control; ///< CHR-select R1, 2 KiB @ PPU $0800.
extern mmc3_register<2> chr2Control; ///< CHR-select R2, 1 KiB @ PPU $1000.
extern mmc3_register<3> chr3Control; ///< CHR-select R3, 1 KiB @ PPU $1400.
extern mmc3_register<4> chr4Control; ///< CHR-select R4, 1 KiB @ PPU $1800.
extern mmc3_register<5> chr5Control; ///< CHR-select R5, 1 KiB @ PPU $1C00.

/**
 * @brief Nametable mirroring select ($A000). Bit 0: 0 = vertical, 1 =
 *        horizontal. Plain direct-address register, unlike the indexed
 *        R0-R7 family above.
 */
extern tech::wo_register<0xa000> mirroring;

/**
 * @brief PRG-RAM enable/write-protect ($A001). Bit 6 = RAM enable, bit 7 =
 *        write-protect. mmc3.cpp's ::_reset enables RAM, writable, at boot.
 */
extern tech::wo_register<0xa001> prgRamProtect;

/**
 * @brief Scanline IRQ reload latch ($C000) -- the value the counter is set
 *        to whenever it reloads. See ::ScheduleScanlineIRQ.
 */
extern tech::wo_register<0xc000> irqLatch;

/**
 * @brief Writes @p ctx into an MMC3 bank register, banking in that
 *        window/CHR slice.
 *
 * Thin, named wrapper over ::mmc3_register's write path, mirroring VRC1's
 * ::SwitchBank shape exactly. @p reg is taken by reference for the same
 * reason ::SwitchBank documents: by value, the hardware poke would still
 * happen, but the shadow update would be lost.
 *
 * Not used by ::_reset (mmc3.cpp): that runs before .bss has been zeroed,
 * so window1Control/window2Control's shadows aren't valid to write yet --
 * see ::_reset's own comment for why it pokes hardware directly instead.
 */
template <u8 Index>
constexpr void SwitchBank(mmc3_register<Index> &reg, u8 ctx) {
    reg = ctx;
}

/**
 * @brief Selects which CHR-ROM bank appears in one of MMC3's six CHR
 *        windows.
 *
 * A named alias for ::SwitchBank restricted to the CHR registers
 * (chr0Control-chr5Control), matching VRC1's separate ::SwitchCHRBank entry
 * point -- MMC3's hardware doesn't actually need a different write path
 * (unlike VRC1's shared chrHighBits read-modify-write), but the distinct
 * name keeps PRG and CHR bank switches visually distinguishable at the call
 * site, same as on VRC1.
 *
 * @param reg  chr0Control through chr5Control; any other instantiation is a
 *             compile error.
 * @param bank Target bank (0-31 for chr0Control/chr1Control's 2 KiB
 *             granularity -- the low bit is ignored by hardware; 0-255 for
 *             chr2Control-chr5Control's 1 KiB granularity).
 */
template <u8 Index>
constexpr void SwitchCHRBank(mmc3_register<Index> &reg, u8 bank) {
    static_assert(Index <= 5, "SwitchCHRBank only valid for chr0Control-chr5Control (R0-R5).");
    reg = bank;
}

/**
 * @brief Arms MMC3's scanline IRQ counter to fire after @p scanline more
 *        PPU A12 rising edges (in practice, roughly @p scanline scanlines
 *        with 8x8 sprites/background rendering enabled).
 *
 * Sets the reload latch ($C000), forces an immediate reload on the next
 * clock ($C001), and enables IRQ generation ($E001). Does not by itself
 * acknowledge any IRQ still pending from a previous split -- call
 * ::AcknowledgeScanlineIRQ first if one might be. Pairs naturally with this
 * project's ::IRQ / ::GetCurrentIRQHandler (interrupts.hpp): a handler
 * reads which split is due, does its work, acknowledges, and schedules the
 * next one.
 *
 * @param scanline Reload value for the countdown; 0 fires on the very next
 *                 qualifying PPU address change.
 */
void ScheduleScanlineIRQ(u8 scanline);

/**
 * @brief Disables further scanline IRQs and clears any currently pending
 *        one ($E000).
 *
 * Purely the acknowledge/disable half -- it does NOT re-enable or re-arm
 * the counter. Call ::ScheduleScanlineIRQ afterward to schedule the next
 * split; keeping the two separate means acknowledging a handler that
 * genuinely has nothing more to schedule this frame doesn't silently leave
 * the counter armed.
 */
void AcknowledgeScanlineIRQ();

/*
 * BANKED CALL scaffolding, mirroring vrc1.hpp's own (see
 * BANKED_CALL_THEORY.txt) as closely as MMC3's two-window hardware allows.
 * Namespaced under mmc3:: (unlike VRC1's global versions) per this file's
 * own convention -- use ::MMC3_BANKED / ::MMC3_BANKED_EXTERN (below,
 * outside the namespace) to register a tagged function; they qualify
 * mmc3::bank_of / mmc3::bank_layout for you.
 */

/// One physical bank/segment's location in the linked ROM image. Same
/// shape and same reasoning as VRC1's section_t: rom_address is
/// LOADADDR()-derived, not the CPU-visible VMA, since every bank that ever
/// aliases onto a switchable window shares one VMA.
struct section_t {
    u32 rom_address;
    u32 size;
};

/// Hardware facts for one tag/domain -- same contract as VRC1's
/// bank_layout<Tag>: primary template undefined (an un-tagged Tag is a
/// compile error), a non-fixed specialization provides
/// `static section_t section()`. See vrc1.hpp's own bank_layout<Tag> doc
/// comment for the constexpr-vs-runtime-section() cost tradeoff, which
/// applies identically here.
template <typename Tag> struct bank_layout;

/// Key for this file's own always-mapped fixed bank ($E000-$FFFF, see
/// ::fixed). Named fixed_bank_tag, not fixed_tag, for the same reason
/// vrc1.hpp's own fixed_bank_tag is: `fixed` is itself a macro in this
/// file, so it can never be used as a bare tag## token.
struct fixed_bank_tag {};

template <> struct bank_layout<fixed_bank_tag> {
    static constexpr bool always_mapped = true;
};

/// Which bank_layout<Tag> a specific tagged function was registered under.
/// One specialization per MMC3_BANKED()/MMC3_BANKED_EXTERN() call site.
/// Primary template deliberately undefined, same reasoning as
/// mmc3::bank_layout<Tag>.
template <auto Fn> struct bank_of;

/// Return-type extraction for a plain function pointer, used by mmc3::Call.
template <typename T> struct function_traits;
template <typename R, typename... A>
struct function_traits<R (*)(A...)> {
    using return_type = R;
};

namespace detail {
    /// Windows 1/2's shared geometry: WINDOW_SIZE bytes each, starting at
    /// WINDOW_BASE ($8000, window1Control's own base). Same constants as
    /// VRC1's -- both chips use 8 KiB windows starting at $8000 -- but only
    /// windowIndex 0/1 are ever valid here (see ::CallInSection's own
    /// comment); MMC3 has no windowIndex-2 register to switch to.
    inline constexpr u16 kWindowBase = 0x8000;
    inline constexpr u16 kWindowSize = 0x2000;

    /**
     * @brief Implementation of ::mmc3::Long / ::CallInSection once the
     *        target register is resolved. Not for direct use.
     *
     * Same shape, and the same LOAD-BEARING [[gnu::noinline]] reasoning, as
     * vrc1_detail::CallInWindow: `fixed` only places an out-of-line symbol,
     * and inlining this into a caller that itself lives in the window being
     * switched away from is a real, reproduced crash on VRC1 (see
     * BANKED_CALL_THEORY.txt) -- the same failure mode applies here
     * unchanged, since it's a property of "switch a bank out from under
     * code that keeps running in that bank," not anything VRC1-specific.
     */
    template <typename TReturn, u8 Index, typename TFunc>
    [[gnu::noinline]] fixed TReturn CallInWindow(mmc3_register<Index> &reg, u8 bank, TFunc fn) {
        SHADOW(reg) {
            SwitchBank(reg, bank);
            return fn();
        }
        __builtin_unreachable(); // SHADOW's scope always runs its body
                                 // exactly once and returns.
    }

    /**
     * @brief Resolves a section_t to whichever of window1Control/
     *        window2Control MMC3 needs, then runs @p fn.
     *
     * Same WINDOW/BANK derivation as vrc1_detail::CallInSection: WINDOW
     * comes from the low 16 bits of rom_address (truncated exactly like a
     * real JSR operand would be), BANK defaults to WINDOW unless
     * bank_layout<Tag>::section() hand-encodes an explicit bank in
     * rom_address's high bits.
     *
     * ONLY windowIndex 0 or 1 are valid ($8000 or $A000) -- MMC3 exposes no
     * register for $C000-$DFFF (this file's header comment), so a
     * bank_layout<Tag>::section() whose address falls at $C000 or above is
     * a caller error, not something this module can route correctly; it is
     * NOT range-checked here (matching vrc1_detail::CallInSection, which
     * also trusts its caller's hand-entered addresses) and falls through to
     * window1Control's case, silently mis-banking. Verify any hand-entered
     * section() address stays below $C000 before relying on it, the same
     * way vrc1.ld's own ASSERTs cross-check its hand-entered bank numbers.
     *
     * There is no CallInWindows2 equivalent (VRC1's oversized-domain,
     * Phase 4 case): MMC3's total register-controlled PRG space is only 16
     * KiB across these two windows, half of VRC1's 24 KiB across three, so
     * a domain spanning both windows at once is unsupported here -- add it
     * the same way vrc1.hpp's CallInWindows2 does if a real need arises.
     */
    template <typename TReturn, typename TFunc>
    [[gnu::noinline]] fixed TReturn CallInSection(const section_t &section, TFunc fn) {
        const u16 vma = static_cast<u16>(section.rom_address);
        const u8 windowIndex = static_cast<u8>((vma - kWindowBase) / kWindowSize);
        const u8 explicitBank = static_cast<u8>(section.rom_address >> 16);
        const u8 bank = explicitBank != 0 ? explicitBank : windowIndex;
        switch (windowIndex) {
            case 1:  return CallInWindow<TReturn>(window2Control, bank, fn);
            default: return CallInWindow<TReturn>(window1Control, bank, fn);
        }
    }
} // namespace detail

/**
 * @brief Calls @p fn with window1Control or window2Control temporarily
 *        re-banked, then restores whatever was banked there before
 *        returning.
 *
 * Manual, non-tag-based counterpart to ::Call -- mirrors VRC1's ::Long
 * exactly, with one fewer valid window (0 or 1 only; MMC3 has no third
 * register-controlled window, see this file's header comment).
 *
 * @tparam TReturn Result type of the call; specify explicitly, e.g.
 *                 `Long<int>(...)`.
 * @param fn     Callable to invoke once the selected window is banked in.
 * @param window Which window to switch: 0 (window1Control/$8000) or 1
 *               (window2Control/$A000); default 0. Off-NES this parameter
 *               is ignored.
 */
template <typename TReturn, typename TFunc>
fixed TReturn Long(TFunc fn, const u8 window = 0) {
#ifdef TARGET_NES
    switch (window) {
        case 1:  return detail::CallInWindow<TReturn>(window2Control, 1, fn);
        default: return detail::CallInWindow<TReturn>(window1Control, 0, fn);
    }
#else
    return fn();
#endif
}

/**
 * @brief Calls a specific, compile-time-known MMC3_BANKED() function,
 *        resolving its bank from mmc3::bank_of<Fn> automatically.
 *
 * Identical shape and behaviour to VRC1's ::Call.
 *
 * @tparam Fn Address of an MMC3_BANKED()/MMC3_BANKED_EXTERN()-tagged
 *            function -- supplied explicitly, e.g. `Call<LoadChunk>(id)`.
 */
template <auto Fn, typename... Args>
fixed auto Call(Args &&...args) -> typename function_traits<decltype(Fn)>::return_type {
    using TReturn = typename function_traits<decltype(Fn)>::return_type;
#ifdef TARGET_NES
    using L = typename bank_of<Fn>::layout;
    if constexpr (L::always_mapped) {
        return Fn(std::forward<Args>(args)...);
    } else {
        return detail::CallInSection<TReturn>(
            L::section(),
            [&]() -> TReturn { return Fn(std::forward<Args>(args)...); });
    }
#else
    return Fn(std::forward<Args>(args)...);
#endif
}

/**
 * @brief Runs an arbitrary block under Fn's resolved window, instead of
 *        calling Fn itself. Identical shape and behaviour to VRC1's
 *        ::CallBlock.
 */
template <auto Fn, typename Block>
fixed auto CallBlock(Block &&block) -> decltype(block()) {
#ifdef TARGET_NES
    using L = typename bank_of<Fn>::layout;
    if constexpr (L::always_mapped) {
        return block();
    } else {
        return detail::CallInSection<decltype(block())>(
            L::section(), std::forward<Block>(block));
    }
#else
    return block();
#endif
}

} // namespace mmc3

/**
 * @brief Declares a tagged, out-of-line C++ function and registers it with
 *        mmc3::bank_of<>, so mmc3::Call/mmc3::CallBlock can resolve its
 *        bank later.
 *
 * Same BANKED -> BANKED_IMPL macro-expansion-order reasoning as VRC1's
 * ::BANKED (see its own comment): forces section_name/tag to their final
 * literal text before # or ## ever sees them. Named MMC3_BANKED rather than
 * plain BANKED specifically so both mapper headers can be seen by the same
 * translation unit without a macro-redefinition clash -- vrc1.hpp's BANKED
 * expands unqualified (bank_of/bank_layout are global there), this one
 * expands qualified to mmc3::, so the two expansions genuinely differ and
 * could not safely share one macro name.
 */
#define MMC3_BANKED(section_name, tag, ret, name, ...) \
    MMC3_BANKED_IMPL(section_name, tag, ret, name, __VA_ARGS__)
#define MMC3_BANKED_IMPL(section_name, tag, ret, name, ...)                      \
    CREATE_SEGMENT_KEYWORD(section_name) ret name(__VA_ARGS__);                 \
    template <> struct mmc3::bank_of<static_cast<ret (*)(__VA_ARGS__)>(&name)> { \
        using layout = mmc3::bank_layout<tag##_tag>;                            \
    }

/**
 * @brief Registers a function with mmc3::bank_of<> without declaring/
 *        placing it -- for hand-written assembly or a separately-assembled
 *        third-party engine. See VRC1's ::BANKED_EXTERN for the full
 *        reasoning; identical here except for the mmc3:: qualification.
 */
#define MMC3_BANKED_EXTERN(section_name, tag, ret, name, ...) \
    MMC3_BANKED_EXTERN_IMPL(section_name, tag, ret, name, __VA_ARGS__)
#define MMC3_BANKED_EXTERN_IMPL(section_name, tag, ret, name, ...)               \
    extern "C" ret name(__VA_ARGS__);                                           \
    template <> struct mmc3::bank_of<static_cast<ret (*)(__VA_ARGS__)>(&name)> { \
        using layout = mmc3::bank_layout<tag##_tag>;                            \
    }
