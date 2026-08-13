#pragma once
#include <platform-nes/platform-nes.hpp>
#include <platform-nes/mappers/mmc3.hpp>

/**
 * @file banks.hpp
 * @brief This demo's own banked PRG-ROM domains: one placement keyword per
 *        logical area of the game, and the bank facts each one maps to.
 *
 * CONSUMER-OWNED, deliberately. mmc3-helper.ld supplies the hardware
 * mechanics (window geometry, the resident image, the fixed bank) and
 * mmc3.hpp supplies the farcall machinery, but which parts of THIS game live
 * in which bank is a project decision -- see BANKED_CALL_THEORY.txt's
 * "REMOVING THE ENFORCED SECTION-NAMING CONVENTION" for why the library
 * never makes it on a project's behalf.
 *
 * FOUR THINGS MUST AGREE for a domain, and only the last two live in this
 * file. Getting them out of step produces a call that banks in the wrong
 * 8 KiB and executes whatever happens to be there -- a crash with no
 * diagnostic, the exact failure BANKED_CALL_THEORY.txt's "CRITICAL
 * POST-PHASE-4 FIX" records from the VRC1 rollout:
 *
 *   1. demo/link.ld's MEMORY region     -- ORIGIN encodes (bank + 1)
 *   2. demo/link.ld's OUTPUT_FORMAT     -- FULL() order IS the bank number
 *   3. the keyword below                -- names the section, hence the region
 *   4. the bank_layout below            -- repeats the encoded address for C++
 *
 * SEVERAL KEYWORDS MAY SHARE ONE BANK, which is the point of separating the
 * keyword from the tag: ::TITLE and ::WORLD both place into .prg_rom_001, so
 * both areas' code shares bank 0's 8 KiB and both bind to bank001_tag. They
 * are separate keywords purely so a future split -- moving world code to its
 * own bank once it outgrows the shared one -- is a one-line change here plus
 * a re-bind, touching no game code at all.
 *
 * WHAT BANKED CODE MAY CALL is the real constraint, and nothing enforces it.
 * While a domain is banked in through window 1, the resident code normally at
 * $8000-$9FFF IS NOT THERE. A banked function may safely call: anything in
 * the fixed bank (::FIXED, $E000-$FFFF), anything the linker placed at
 * $A000-$DFFF (the other two resident windows, which this file's domains
 * never switch), and anything in its own bank. It may NOT call ordinary code
 * that happens to have landed in $8000-$9FFF -- and which functions those are
 * is decided by the linker, not visible in the source. Keep banked code
 * self-contained or pointed at ::FIXED helpers.
 */

/// @name Placement keywords
/// Tag a function or variable definition with one of these to place it in
/// that bank, exactly as ::FIXED places into $E000-$FFFF:
/// `namespace title { TITLE void main() { ... } }`. Uppercase per this
/// project's macro convention. Expands to nothing off-NES, where every
/// backend is one flat address space with no banking at all.
///
/// THREE ATTRIBUTES, ALL LOAD-BEARING -- a bare section attribute is not
/// enough to put something in a bank and keep it there:
///
/// - noinline: a section attribute only places an OUT-OF-LINE copy. Without
///   this, LTO inlines a small banked function into its (resident) caller,
///   drops the out-of-line copy as unreferenced, and the domain silently
///   ends up empty. Observed on the first build of this file.
/// - used, retain: banked content must survive even when nothing statically
///   reaches it. A game mode is dispatched through a variable, so whether it
///   is "reachable" depends on what the optimizer can prove about that
///   variable -- and when it proved gameMode never becomes Title, it deleted
///   both functions below and emptied the bank. Content you deliberately
///   placed in a bank should be in the ROM because you put it there, not
///   because the optimizer failed to prove it dead. `used` holds it through
///   LTO, `retain` through --gc-sections.
///
/// The cost is honest: genuinely dead banked code stays in the ROM. That is
/// the right trade for a bank, which is space you have already paid for.
/// @{
#define TITLE CREATE_SEGMENT_KEYWORD(".prg_rom_001") __attribute__((noinline, used, retain)) ///< Title screen -> bank 0, shared with ::WORLD.
#define WORLD CREATE_SEGMENT_KEYWORD(".prg_rom_001") __attribute__((noinline, used, retain)) ///< World map -> bank 0, shared with ::TITLE.
/// @}

/// @name Bank tags
/// The key ::MMC3_BIND takes, minus its `_tag` suffix -- `MMC3_BIND(
/// title::main, bank001)`. Named after the SECTION rather than the game area
/// on purpose: a tag identifies one physical 8 KiB bank, which is exactly
/// what several keywords can point at together.
/// @{
struct bank001_tag {}; ///< .prg_rom_001, physical bank 0, window 1. See ::TITLE / ::WORLD.
struct bank002_tag {}; ///< .prg_rom_002, physical bank 1, window 1. Audio engine CODE.
struct bank003_tag {}; ///< .prg_rom_003, physical bank 2, WINDOW 2. Audio engine DATA.
/// @}

#ifdef TARGET_NES
/**
 * @brief Bank 0's location, as the farcall machinery needs it.
 *
 * 0x00018000 = ((0 + 1) << 16) | 0x8000: physical bank 0, reached through
 * window 1 ($8000-$9FFF, R6). The +1 bias is mmc3.hpp's own encoding, NOT an
 * off-by-one -- see section_t's ENCODING comment there, and the matching
 * ORIGIN in demo/link.ld. MUST equal that ORIGIN exactly.
 *
 * constexpr, which is what makes this free: mmc3::Call folds the whole
 * window/bank resolution away at compile time, leaving just the two register
 * writes. A runtime section() computed from linker symbols costs ~120 bytes
 * per call site instead (measured -- see vrc1.hpp's bank_layout comment), and
 * is only needed for a domain sharing a region with unrelated content, which
 * none of these do.
 */
template <> struct mmc3::bank_layout<bank001_tag> {
    static constexpr bool always_mapped = false;
    static constexpr mmc3::section_t section() { return { 0x00018000u, 0x2000u }; }
};

/// Audio engine CODE. 0x00028000 = bank 1, window 1 ($8000, R6).
template <> struct mmc3::bank_layout<bank002_tag> {
    static constexpr bool always_mapped = false;
    static constexpr mmc3::section_t section() { return { 0x00028000u, 0x2000u }; }
};

/**
 * @brief Audio engine DATA. 0x0003a000 = bank 2, WINDOW 2 ($A000, R7).
 *
 * The $a000 is the whole point, not a typo: this bank is mapped at the same
 * time as bank002_tag's, via mmc3::CallPairedBlock, because the engine walks
 * song data while it executes. Two switchable windows means the pair must use
 * one each -- CallPairedBlock static_asserts it.
 */
template <> struct mmc3::bank_layout<bank003_tag> {
    static constexpr bool always_mapped = false;
    static constexpr mmc3::section_t section() { return { 0x0003a000u, 0x2000u }; }
};

/// @name Audio backend roles
/// platform-nes declares ::audio_code_bank_tag / ::audio_data_bank_tag (see
/// audio.hpp) but leaves the banks to the project, since which bank holds
/// what is a layout decision. This demo points them at banks 1 and 2, so
/// src/nes/audio.cpp's farcalls resolve to those. A project not banking its
/// audio would instead give both `always_mapped = true` and pay nothing.
///
/// Aliases rather than new tags: the engine has no bank of its own, it just
/// occupies one this file already named.
/// @{
template <> struct mmc3::bank_layout<audio_code_bank_tag> : mmc3::bank_layout<bank002_tag> {};
template <> struct mmc3::bank_layout<audio_data_bank_tag> : mmc3::bank_layout<bank003_tag> {};
/// @}
#endif
