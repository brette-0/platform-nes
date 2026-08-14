#pragma once
#include <platform-nes/platform-nes.hpp>
#include <platform-nes/mappers/mmc3.hpp>

/**
 * @file banks.hpp
 * @brief This demo's own PRG-ROM domains: where each one physically lives, in
 *        the form the farcall machinery needs.
 *
 * CONSUMER-OWNED, deliberately. mmc3-helper.ld supplies the hardware mechanics
 * (window geometry, the fixed bank) and mmc3.hpp supplies the farcall
 * machinery, but which parts of THIS game live in which bank is a project
 * decision -- see BANKED_CALL_THEORY.txt's "REMOVING THE ENFORCED
 * SECTION-NAMING CONVENTION" for why the library never makes it on a
 * project's behalf.
 *
 * A TAG NAMES A REAL BANK. If you have a tag, the call switches to it. Code
 * already in reach -- ::FIXED, or whatever the caller knows is mapped -- is
 * reached by an ordinary call and needs nothing from this file. There is no
 * "always mapped" layout, because a layout that named no bank was a
 * contradiction that only existed to let unbanked code go through the banked
 * spelling.
 *
 * THREE THINGS MUST AGREE per domain, and only the last lives here. Getting
 * them out of step produces a call that banks in the wrong 8 KiB and executes
 * whatever is there -- a crash with no diagnostic, the exact failure
 * BANKED_CALL_THEORY.txt's "CRITICAL POST-PHASE-4 FIX" records from the VRC1
 * rollout:
 *
 *   1. demo/link.ld's MEMORY region  -- ORIGIN encodes (bank + 1)
 *   2. demo/link.ld's OUTPUT_FORMAT  -- FULL() order IS the bank number
 *   3. the bank_layout below         -- repeats that encoded address for C++
 *
 * WHAT BANKED CODE MAY CALL is the real constraint, and nothing enforces it.
 * This ROM runs one resident window ($C000-$DFFF) plus the fixed bank; both
 * switchable windows belong to the level domain. So banked code may call
 * anything in ::FIXED, anything at $C000-$DFFF, and anything in its own
 * domain -- and nothing else, because nothing else is in the address space
 * while it runs.
 */

/// @name Domain tags
/// The key ::MMC3_BIND takes, minus its `_tag` suffix. Named after the
/// section rather than the game area on purpose: a tag identifies physical
/// banks, which several areas of the game may share.
/// @{
struct audio_tag      {}; ///< .prg_rom_audio, bank 2, window 1. Engine + pnes audio module.
struct audio_data_tag {}; ///< .prg_rom_audio_data, bank 3, WINDOW 2. Songs + SFX.
struct level_tag {}; ///< .prg_rom_level, physical banks 0+1, BOTH windows. See ::EnterLevelBanks.
/// @}

#ifdef TARGET_NES
/**
 * @brief The LEVEL domain: banks 0 AND 1, at $8000-$BFFF, both windows.
 *
 * 0x00018000 = ((0 + 1) << 16) | 0x8000: physical bank 0 through window 1. The
 * +1 bias is mmc3.hpp's own encoding, not an off-by-one -- see section_t's
 * ENCODING comment there. The 0x4000 size is what makes this two banks rather
 * than one, and must match demo/link.ld's prg_rom_level LENGTH.
 *
 * NOT REACHABLE BY FARCALL, and deliberately has no MMC3_BIND anywhere:
 * mmc3::CallInSection derives ONE window index from the address and switches
 * ONE register, so a farcall would map bank 0 at $8000 and leave $A000 showing
 * whatever was there -- half the domain missing, no diagnostic. Entered by
 * ::EnterLevelBanks() instead, once, on mode entry.
 *
 * constexpr, which is what makes it free: the window/bank resolution folds
 * away at compile time, leaving just the register writes. A runtime section()
 * computed from linker symbols costs ~120 bytes per call site instead
 * (measured -- see vrc1.hpp's bank_layout comment).
 */
template <> struct mmc3::bank_layout<level_tag> {
    static constexpr mmc3::section_t section() { return { 0x00018000u, 0x4000u }; }
};

/**
 * @brief The AUDIO CODE bank: FamiStudio's engine and platform-nes's audio
 *        module. 0x00038000 = bank 2, window 1 ($8000, R6).
 *
 * THOSE TWO SHARING A BANK IS THE CONTRACT, not packing convenience.
 * src/nes/audio.cpp contains no bank switching whatsoever -- it reaches
 * famistudio_update() with a plain call, valid only because the engine is
 * mapped whenever the module is. PLATFORM_NES_AUDIO_SECTION (local.cmake) and
 * FAMISTUDIO_CA65_CODE_SEGMENT (demo/famistudio_config.s) put them here.
 *
 * The song and SFX data is NOT here, deliberately -- see ::audio_data_tag.
 */
template <> struct mmc3::bank_layout<audio_tag> {
    static constexpr mmc3::section_t section() { return { 0x00038000u, 0x2000u }; }
};

/**
 * @brief The AUDIO DATA bank: songs and SFX. 0x0004a000 = bank 3, WINDOW 2.
 *
 * A separate bank because data has no reason to sit beside the engine, and
 * forcing it there would cap every song this cart can hold at whatever is left
 * of bank 2. The $a000 is the one part that is not free choice: the engine
 * walks this data while it executes, so the two must be mapped simultaneously
 * and therefore cannot share a window. mmc3::CallPairedBlock static_asserts it.
 */
template <> struct mmc3::bank_layout<audio_data_tag> {
    static constexpr mmc3::section_t section() { return { 0x0004a000u, 0x2000u }; }
};

/**
 * @brief Runs @p block with the audio code bank AND its data bank mapped.
 *
 * Both, because the engine reads song data while executing. Wrapping the call
 * rather than binding each entry point with MMC3_BIND: a bind switches one
 * bank, and every audio call needs two.
 */
template <typename Block>
decltype(auto) InAudioBanks(Block &&block) {
    return mmc3::CallPairedBlock<audio_tag, audio_data_tag>(
        static_cast<Block &&>(block));
}
#endif

/**
 * @brief Maps the level domain into both switchable windows. Call once, on
 *        entry to level mode; there is no matching "leave".
 *
 * R6 = 0 ($8000-$9FFF), R7 = 1 ($A000-$BFFF) -- the two banks prg_rom_level
 * spans, in the order the linker laid them out. Must be called from code that
 * is NOT in either window, i.e. from $C000-$DFFF or ::FIXED, or it unmaps
 * itself mid-execution. main.cpp's dispatcher is ::FIXED and satisfies this.
 *
 * Audio farcalls made from inside level still work untouched: their thunks
 * live in the fixed bank, save both shadows, map the audio pair, and restore
 * level's pair before returning. What must NOT happen is an interrupt reaching
 * code that is currently banked out -- which is why level's vector handlers
 * are ::FIXED (level.cpp).
 */
inline void EnterLevelBanks() {
#ifdef TARGET_NES
    mmc3::SwitchBank(mmc3::window1Control, 0);
    mmc3::SwitchBank(mmc3::window2Control, 1);
#endif
}
