#pragma once
#include <platform-nes/platform-nes.hpp>
#include <platform-nes/mappers/mmc3.hpp>
#include "modes/level.hpp"   // level::UpdateActors, for ::MMC3_BIND below

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
struct level_code_tag    {}; ///< .prg_rom_level_code, bank 0, window 1. Level's own code. See ::EnterLevelBanks.
struct level_data_tag    {}; ///< .prg_rom_level_data, bank 1, WINDOW 2. Static plane + DynLengths, pinned for the session.
struct level_dynamic_tag {}; ///< .prg_rom_level_dynamic, bank 6, WINDOW 2. DynData ROM source, farcalled once at load.
struct cold_tag  {}; ///< .prg_rom_cold, bank 4, window 1. Code that runs once and never again.
struct actor_tag {}; ///< .prg_rom_actors, bank 5, window 1. Actor + Player code.
/// @}

#ifdef TARGET_NES
/**
 * @brief The LEVEL CODE domain: bank 0, window 1 ($8000, R6).
 *        0x00018000 = ((0 + 1) << 16) | 0x8000.
 *
 * Level's own code -- ::LEVEL_CODE-tagged in levels.cpp/collision_map.cpp:
 * main(), LoadLevel, ColMapSeed/Stamp/Track/SlideLeft/SlideRight. Paired with
 * ::level_data_tag (window 2) the same way ::audio_tag pairs with
 * ::audio_data_tag -- this code reads the static plane and the dynamic
 * plane's run-lengths directly out of window 2 while it runs, so the two
 * must be mapped together.
 *
 * NOT REACHABLE BY FARCALL, same reasoning as the combined level domain this
 * replaces: mmc3::CallInSection derives ONE window index from the address
 * and switches ONE register, so a farcall would map this bank at $8000 and
 * leave $A000 (::level_data_tag) showing whatever was there -- half the
 * level missing, no diagnostic. Entered by ::EnterLevelBanks() instead, once,
 * on mode entry, alongside ::level_data_tag.
 *
 * EnterLevelSetup (::COLD) still needs to reach LoadLevel/ColMapSeed once, at
 * load -- see its own mmc3::CallInBlock<level_code_tag> wrap in level.cpp,
 * the same pattern Player::Reset's block uses to reach ::actor_tag from COLD.
 *
 * constexpr, which is what makes it free: the window/bank resolution folds
 * away at compile time, leaving just the register writes. A runtime section()
 * computed from linker symbols costs ~120 bytes per call site instead
 * (measured -- see vrc1.hpp's bank_layout comment).
 */
template <> struct mmc3::bank_layout<level_code_tag> {
    static constexpr mmc3::section_t section() { return { 0x00018000u, 0x2000u }; }
};

/**
 * @brief The LEVEL DATA domain: bank 1, WINDOW 2 ($A000, R7).
 *        0x0002a000 = ((1 + 1) << 16) | 0xA000.
 *
 * The two ROM tables level's own code reads CONTINUOUSLY -- ::LEVEL_DATA-tagged
 * in levels.cpp:
 *   - the STATIC PLANE (TileData_1_1). ColMapStamp's `stat.dp` is a flat ROM
 *     pointer into this bank, walked on every column the composite window
 *     admits, for as long as the level runs.
 *   - DynLengths_1_1 (dynamic-plane run LENGTHS). DynamicCursor never copies
 *     lengths to RAM (dynamic.hpp's own comment: "a DynamicCursor streams
 *     lengths from ROM but peeks data from RAM"), so it's read just as
 *     continuously as the static plane, and belongs beside it.
 *
 * DynData_1_1 (dynamic-plane run CONTENT) is deliberately NOT here -- see
 * ::level_dynamic_tag. It's read ONCE, at load, never again on any per-frame
 * path, so it doesn't earn permanent residency in the bank that has to stay
 * mapped for the whole session.
 *
 * WINDOW 2 is not a free choice, same constraint as ::audio_data_tag: this
 * must be mappable AT THE SAME TIME as ::level_code_tag (window 1), since
 * that code reads the static plane and DynLengths directly, so the two
 * cannot share a window. Entered together by ::EnterLevelBanks() and held for
 * the whole level session.
 */
template <> struct mmc3::bank_layout<level_data_tag> {
    static constexpr mmc3::section_t section() { return { 0x0002a000u, 0x2000u }; }
};

/**
 * @brief The LEVEL DYNAMIC-SOURCE domain: bank 6, WINDOW 2 ($A000, R7).
 *        0x0007a000 = ((6 + 1) << 16) | 0xA000.
 *
 * DynData_1_1 (dynamic-plane run CONTENT), ::LEVEL_DYNAMIC-tagged in
 * levels.cpp. Read ONCE per level, by LoadDynamicLayer, which copies it into
 * the RAM DynData[] array (plain .bss, unrelated to this bank) -- never
 * touched again on any per-frame path, only if the dynamic plane is ever
 * reset from its ROM source.
 *
 * REACHABLE BY FARCALL, unlike ::level_data_tag: this is NOT permanently
 * pinned alongside ::level_code_tag. LoadLevel (::LEVEL_CODE) wraps its call
 * to LoadDynamicLayer in mmc3::CallInBlock<level_dynamic_tag>, which
 * temporarily swaps window 2 away from ::level_data_tag (the static plane),
 * runs the copy, and restores window 2 to ::level_data_tag on return -- so
 * the static plane is unmapped only for the duration of that one copy, and
 * every other window-2 consumer for the rest of the session sees
 * ::level_data_tag exactly as if this bank didn't exist. Also WINDOW 2, same
 * reason as ::level_data_tag: LoadDynamicLayer's copy loop is the one thing
 * that reads it, so it costs nothing to keep off window 1's already-tighter
 * budget.
 */
template <> struct mmc3::bank_layout<level_dynamic_tag> {
    static constexpr mmc3::section_t section() { return { 0x0007a000u, 0x2000u }; }
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
 * @brief The COLD domain: bank 4, window 1 ($8000, R6). 0x00058000 = bank 4.
 *
 * Home for code that runs once, ever, and doesn't earn permanent residency in
 * a domain that has to stay mapped -- level's own one-time level-entry setup,
 * currently. UNLIKE ::level_code_tag this IS reachable by farcall: it only
 * needs one window, so an ordinary mmc3::CallInBlock<cold_tag> (see
 * ::InColdBank, below) switches, runs, and restores window 1 -- safe to call
 * even from inside level's own domain, the same way ::InAudioBanks already is
 * (the trampoline lives in the fixed bank, unaffected by whatever window 1
 * shows mid-call).
 */
template <> struct mmc3::bank_layout<cold_tag> {
    static constexpr mmc3::section_t section() { return { 0x00058000u, 0x2000u }; }
};

/**
 * @brief The ACTORS domain: bank 5, window 1 ($8000, R6). 0x00068000 = bank 5.
 *
 * Actor's own methods (Move/Start/Update) and Player's (Update/Reset) --
 * ::ACTORS-tagged at their definitions in actor.cpp/player.cpp, the same
 * placement mechanism ::FIXED and ::COLD already use, which is what keeps
 * them out of the generic resident catch-all even though Player is
 * level::-namespaced.
 *
 * REACHABLE BY FARCALL, unlike ::level_code_tag: it is exactly one window, so
 * an ordinary mmc3::CallInBlock<actor_tag> switches, runs, and restores
 * window 1. Called ONCE per frame from level::main(), wrapping a
 * single UpdateActors() entry point that updates the whole roster (both
 * players today, NPCs later) behind that one switch -- not once per actor,
 * which would pay the window-switch cost on the hottest loop in the frame.
 *
 * WHAT THIS BANK MAY CALL, same constraint as every other banked domain (see
 * this file's own header comment): ::FIXED, whatever is at $C000-$DFFF
 * (prg_rom_switchable), and its own contents. level::Blocked/CollectCoins/
 * CollectCoins2 are ::FIXED for exactly this reason -- Player::Update calls
 * them, so they have to be reachable no matter what else is mapped in window 1.
 */
template <> struct mmc3::bank_layout<actor_tag> {
    static constexpr mmc3::section_t section() { return { 0x00068000u, 0x2000u }; }
};

#endif

/**
 * @brief Pins a function into the COLD bank's own section (.prg_rom_cold).
 *
 * Placement and the ::InColdBank/CallInBlock<cold_tag> call site are
 * independent -- nothing checks they agree (see this file's own header
 * comment, "THREE THINGS MUST AGREE"). A function reached through
 * ::InColdBank must carry ::COLD or it stays wherever its namespace's own
 * placement rule sends it instead (the generic resident catch-all, once
 * ::LEVEL_CODE/::LEVEL_DATA/::ACTORS have taken everything else that used to
 * have a special level-domain rule -- see demo/link.ld) and the farcall
 * banks in the wrong 8 KiB. Expands to nothing off-NES, same as mmc3.hpp's
 * ::FIXED.
 */
#define COLD CREATE_SEGMENT_KEYWORD(".prg_rom_cold")

/**
 * @brief Pins a function into the level's CODE bank (.prg_rom_level_code) --
 *        see ::level_code_tag.
 *
 * Everything level's own control flow needs, permanently mapped together in
 * window 1 for the whole level session: main(), LoadLevel,
 * ColMapSeed/Stamp/Track/SlideLeft/SlideRight. Expands to nothing off-NES.
 */
#define LEVEL_CODE CREATE_SEGMENT_KEYWORD(".prg_rom_level_code")

/**
 * @brief Pins a variable into the level's DATA bank (.prg_rom_level_data) --
 *        see ::level_data_tag.
 *
 * The static plane (TileData_1_1) and DynLengths_1_1 -- the two ROM tables
 * read CONTINUOUSLY, not DynData_1_1 (see ::LEVEL_DYNAMIC below). Data only:
 * nothing calls into this bank, it's read as plain ROM pointers by
 * ::LEVEL_CODE while ::EnterLevelBanks keeps both mapped for the whole
 * level. Expands to nothing off-NES.
 */
#define LEVEL_DATA CREATE_SEGMENT_KEYWORD(".prg_rom_level_data")

/**
 * @brief Pins a variable into the level's DYNAMIC-SOURCE bank
 *        (.prg_rom_level_dynamic) -- see ::level_dynamic_tag.
 *
 * DynData_1_1 only: read ONCE, at load, through an explicit
 * mmc3::CallInBlock<level_dynamic_tag> (LoadLevel, levels.cpp) rather than
 * being permanently mapped alongside ::LEVEL_DATA. Expands to nothing
 * off-NES.
 */
#define LEVEL_DYNAMIC CREATE_SEGMENT_KEYWORD(".prg_rom_level_dynamic")

/**
 * @brief Runs @p block once, in the COLD bank, restoring window 1 after.
 *
 * Off-NES this collapses to a plain call (::CallInBlock does), so call sites
 * are identical on every backend -- same shape as ::InAudioBanks.
 */
template <typename Block>
decltype(auto) InColdBank(Block &&block) {
    return mmc3::CallInBlock<cold_tag>(static_cast<Block &&>(block));
}

/**
 * @brief Pins a function into the ACTORS bank's own section (.prg_rom_actors).
 *
 * Placement and the ::MMC3_BIND registration below are independent -- nothing
 * checks they agree (same "THREE THINGS MUST AGREE" caveat as ::COLD). A
 * function reached through mmc3::Call<level::UpdateActors>/CallInBlock<...>
 * must carry ::ACTORS or it stays wherever its namespace's own placement rule
 * sends it instead (the generic resident catch-all -- level:: code has no
 * special wildcard rule of its own any more, see demo/link.ld) and the
 * farcall banks in the wrong 8 KiB.
 */
#define ACTORS CREATE_SEGMENT_KEYWORD(".prg_rom_actors")

/**
 * @brief Registers level::UpdateActors with mmc3::bank_of<>, so
 *        mmc3::Call<level::UpdateActors>() can resolve the actors bank
 *        instead of a project-local wrapper naming the tag by hand.
 *
 * ::MMC3_BIND rather than ::MMC3_BANKED because UpdateActors is defined in
 * player.cpp inside `namespace level`, where the ::ACTORS placement keyword
 * already sits at its definition -- this just binds the qualified name to
 * the tag at global scope, which is where bank_of<> specializations have to
 * live. See mmc3.hpp's own ::MMC3_BIND comment for the split.
 *
 * Used for the per-frame call site only (level.cpp's main loop). Level
 * setup's Reset pair goes through mmc3::CallInBlock<actor_tag> directly
 * instead, deliberately NOT through this binding -- that block has nothing
 * to do with UpdateActors, so naming the tag it needs is more honest than
 * borrowing an unrelated function's registration as a roundabout way to say
 * so. See that call site's own comment in level.cpp.
 */
MMC3_BIND(level::UpdateActors, actor);

/**
 * @brief This project's two audio banks, bound into audio::InBanks.
 *
 * The REQUIREMENT (code and data mapped together, in different windows) is the
 * library's and lives in audio.hpp. All this supplies is which banks -- the
 * one thing the library cannot know. Off-NES it collapses to a plain call, so
 * call sites are identical on every backend.
 */
template <typename Block>
decltype(auto) InAudioBanks(Block &&block) {
    return audio::InBanks<mmc3, audio_tag, audio_data_tag>(
        static_cast<Block &&>(block));
}

/**
 * @brief Pins level_code_tag (window 1) and level_data_tag (window 2) for
 *        the whole level session -- level's own code, and everything it
 *        reads (static plane, dynamic-plane ROM source) -- entered together
 *        and never farcalled (see both tags' own comments for why: this
 *        code reads both planes directly, with no call boundary to hang a
 *        farcall off). Called once, on mode entry (main.cpp), not from
 *        inside the level loop.
 */
inline void EnterLevelBanks() {
#ifdef TARGET_NES
    mmc3::SwitchBank(mmc3::window1Control, 0);
    mmc3::SwitchBank(mmc3::window2Control, 1);
#endif
}
