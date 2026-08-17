#pragma once
#include <platform-nes/platform-nes.hpp>
#include <platform-nes/mappers/mmc3.hpp>
#include "modes/level.hpp"   // level::UpdateActors, for ::MMC3_BIND below

/**
 * @file banks.hpp
 * @brief This demo's own PRG-ROM domains: where each one physically lives.
 *
 * CONSUMER-OWNED: mmc3-helper.ld/mmc3.hpp supply the mechanics, but which
 * parts of this game live in which bank is a project decision.
 *
 * A TAG NAMES A REAL BANK. Code already in reach (::FIXED, or whatever the
 * caller knows is mapped) needs nothing from this file.
 *
 * THREE THINGS MUST AGREE per domain, and only the last lives here -- out of
 * step means a call banks in the wrong 8 KiB with no diagnostic:
 *   1. demo/link.ld's MEMORY region  -- ORIGIN encodes (bank + 1)
 *   2. demo/link.ld's OUTPUT_FORMAT  -- FULL() order IS the bank number
 *   3. the bank_layout below         -- repeats that encoded address for C++
 *
 * WHAT BANKED CODE MAY CALL: this ROM runs one resident window ($C000-$DFFF)
 * plus the fixed bank. Banked code may call ::FIXED, $C000-$DFFF, and its own
 * domain -- nothing else is in the address space while it runs.
 */

/// @name Domain tags
/// The key ::MMC3_BIND takes, minus its `_tag` suffix.
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
 * @brief LEVEL CODE: bank 0, window 1 ($8000, R6). 0x00018000 = (0+1)<<16 | 0x8000.
 *
 * Level's own code (::LEVEL_CODE-tagged): main(), LoadLevel,
 * ColMapSeed/Stamp/Track/SlideLeft/SlideRight. Paired with ::level_data_tag
 * (window 2), which this code reads directly while it runs.
 *
 * NOT REACHABLE BY FARCALL: a farcall only switches one window, so it would
 * map this at $8000 and leave $A000 (::level_data_tag) stale. Entered by
 * ::EnterLevelBanks() once, on mode entry, alongside ::level_data_tag.
 * EnterLevelSetup (::COLD) reaches LoadLevel/ColMapSeed once via an explicit
 * mmc3::CallInBlock<level_code_tag> -- see that call site in level.cpp.
 */
template <> struct mmc3::bank_layout<level_code_tag> {
    static constexpr mmc3::section_t section() { return { 0x00018000u, 0x2000u }; }
};

/**
 * @brief LEVEL DATA: bank 1, WINDOW 2 ($A000, R7). 0x0002a000 = (1+1)<<16 | 0xA000.
 *
 * The two ROM tables read CONTINUOUSLY: the static plane (TileData_1_1) and
 * DynLengths_1_1 (dynamic-plane run lengths -- DynamicCursor streams these
 * from ROM every step, never copies to RAM).
 *
 * DynData_1_1 (run CONTENT) is deliberately NOT here -- see
 * ::level_dynamic_tag; it's read once at load, not worth permanent residency.
 *
 * WINDOW 2 is forced: must be mappable alongside ::level_code_tag (window 1)
 * since that code reads this directly. Entered together by
 * ::EnterLevelBanks() for the whole level session.
 */
template <> struct mmc3::bank_layout<level_data_tag> {
    static constexpr mmc3::section_t section() { return { 0x0002a000u, 0x2000u }; }
};

/**
 * @brief LEVEL DYNAMIC-SOURCE: bank 6, WINDOW 2 ($A000, R7). 0x0007a000 = (6+1)<<16 | 0xA000.
 *
 * DynData_1_1 (dynamic-plane run CONTENT). Read once per level by
 * LoadDynamicLayer, which copies it into the RAM DynData[] array; untouched
 * on any per-frame path.
 *
 * REACHABLE BY FARCALL, unlike ::level_data_tag: LoadLevel (::LEVEL_CODE)
 * wraps the LoadDynamicLayer call in mmc3::CallInBlock<level_dynamic_tag>,
 * which swaps window 2 away from ::level_data_tag, copies, and restores it --
 * the static plane is unmapped only for that one copy.
 */
template <> struct mmc3::bank_layout<level_dynamic_tag> {
    static constexpr mmc3::section_t section() { return { 0x0007a000u, 0x2000u }; }
};

/**
 * @brief AUDIO CODE: bank 2, window 1 ($8000, R6). 0x00038000.
 *
 * FamiStudio's engine and platform-nes's audio module -- sharing a bank is
 * the contract: src/nes/audio.cpp calls famistudio_update() with a plain
 * call, valid only because the engine is always mapped alongside it.
 * PLATFORM_NES_AUDIO_SECTION / FAMISTUDIO_CA65_CODE_SEGMENT put them here.
 * Song/SFX data is NOT here -- see ::audio_data_tag.
 */
template <> struct mmc3::bank_layout<audio_tag> {
    static constexpr mmc3::section_t section() { return { 0x00038000u, 0x2000u }; }
};

/**
 * @brief AUDIO DATA: bank 3, WINDOW 2. 0x0004a000.
 *
 * Songs and SFX, separate from the engine so cart song capacity isn't capped
 * by bank 2's leftover space. WINDOW 2 is forced: the engine walks this data
 * while executing, so both must be mapped simultaneously (mmc3::CallPairedBlock
 * static_asserts it).
 */
template <> struct mmc3::bank_layout<audio_data_tag> {
    static constexpr mmc3::section_t section() { return { 0x0004a000u, 0x2000u }; }
};

/**
 * @brief COLD: bank 4, window 1 ($8000, R6). 0x00058000.
 *
 * Code that runs once, ever -- level's one-time entry setup, currently.
 * REACHABLE BY FARCALL (one window): mmc3::CallInBlock<cold_tag> (see
 * ::InColdBank) switches, runs, restores -- safe even from inside level's
 * own domain, same as ::InAudioBanks.
 */
template <> struct mmc3::bank_layout<cold_tag> {
    static constexpr mmc3::section_t section() { return { 0x00058000u, 0x2000u }; }
};

/**
 * @brief ACTORS: bank 5, window 1 ($8000, R6). 0x00068000.
 *
 * Actor's own methods (Move/Start/Update) and Player's (Update/Reset),
 * ::ACTORS-tagged in actor.cpp/player.cpp.
 *
 * REACHABLE BY FARCALL (one window): mmc3::CallInBlock<actor_tag> switches,
 * runs, restores. Called ONCE per frame from level::main() via a single
 * UpdateActors() entry point covering the whole roster, not once per actor.
 *
 * WHAT THIS BANK MAY CALL: ::FIXED, $C000-$DFFF, and its own contents.
 * level::Blocked/CollectCoins/CollectCoins2 are ::FIXED because
 * Player::Update needs them reachable no matter what else is mapped.
 */
template <> struct mmc3::bank_layout<actor_tag> {
    static constexpr mmc3::section_t section() { return { 0x00068000u, 0x2000u }; }
};

#endif

/**
 * @brief Pins a function into the COLD bank's own section (.prg_rom_cold).
 *
 * Placement and the ::InColdBank call site are independent -- nothing checks
 * they agree ("THREE THINGS MUST AGREE" above). Omitting ::COLD leaves the
 * function in the generic resident catch-all and the farcall banks in the
 * wrong 8 KiB. Expands to nothing off-NES.
 */
#define COLD CREATE_SEGMENT_KEYWORD(".prg_rom_cold")

/**
 * @brief Pins a function into the level's CODE bank (.prg_rom_level_code) --
 *        see ::level_code_tag.
 *
 * main(), LoadLevel, ColMapSeed/Stamp/Track/SlideLeft/SlideRight -- level's
 * own control flow, permanently mapped for the whole level session. Expands
 * to nothing off-NES.
 */
#define LEVEL_CODE CREATE_SEGMENT_KEYWORD(".prg_rom_level_code")

/**
 * @brief Pins a variable into the level's DATA bank (.prg_rom_level_data) --
 *        see ::level_data_tag.
 *
 * TileData_1_1 and DynLengths_1_1 only -- not DynData_1_1 (see
 * ::LEVEL_DYNAMIC). Data only, read as plain ROM pointers. Expands to
 * nothing off-NES.
 */
#define LEVEL_DATA CREATE_SEGMENT_KEYWORD(".prg_rom_level_data")

/**
 * @brief Pins a variable into the level's DYNAMIC-SOURCE bank
 *        (.prg_rom_level_dynamic) -- see ::level_dynamic_tag.
 *
 * DynData_1_1 only, reached via mmc3::CallInBlock<level_dynamic_tag>
 * (LoadLevel, levels.cpp) rather than permanent mapping. Expands to nothing
 * off-NES.
 */
#define LEVEL_DYNAMIC CREATE_SEGMENT_KEYWORD(".prg_rom_level_dynamic")

/**
 * @brief Runs @p block once, in the COLD bank, restoring window 1 after.
 *
 * Off-NES collapses to a plain call -- same shape as ::InAudioBanks.
 */
template <typename Block>
decltype(auto) InColdBank(Block &&block) {
    return mmc3::CallInBlock<cold_tag>(static_cast<Block &&>(block));
}

/**
 * @brief Pins a function into the ACTORS bank's own section (.prg_rom_actors).
 *
 * Placement and the ::MMC3_BIND registration below are independent -- same
 * "THREE THINGS MUST AGREE" caveat as ::COLD. Omitting ::ACTORS leaves the
 * function in the generic resident catch-all and the farcall banks wrong.
 */
#define ACTORS CREATE_SEGMENT_KEYWORD(".prg_rom_actors")

/**
 * @brief Registers level::UpdateActors with mmc3::bank_of<>, so
 *        mmc3::Call<level::UpdateActors>() can resolve the actors bank.
 *
 * ::MMC3_BIND rather than ::MMC3_BANKED: UpdateActors is already defined
 * (with ::ACTORS at its definition) in player.cpp; this just binds the
 * qualified name to the tag at global scope, where bank_of<> specializations
 * must live.
 *
 * Used for the per-frame call site only. Level setup's Reset pair goes
 * through mmc3::CallInBlock<actor_tag> directly instead -- that block has
 * nothing to do with UpdateActors, so naming the tag it needs is more honest.
 * See that call site's comment in level.cpp.
 */
MMC3_BIND(level::UpdateActors, actor);

/**
 * @brief This project's two audio banks, bound into audio::InBanks.
 *
 * The library owns the requirement (code+data mapped together, in different
 * windows); this just supplies which banks. Off-NES collapses to a plain call.
 */
template <typename Block>
decltype(auto) InAudioBanks(Block &&block) {
    return audio::InBanks<mmc3, audio_tag, audio_data_tag>(
        static_cast<Block &&>(block));
}

/**
 * @brief Pins level_code_tag (window 1) and level_data_tag (window 2) for
 *        the whole level session. Never farcalled: level code reads both
 *        planes directly, with no call boundary to hang a farcall off.
 *        Called once, on mode entry (main.cpp).
 */
inline void EnterLevelBanks() {
#ifdef TARGET_NES
    mmc3::SwitchBank(mmc3::window1Control, 0);
    mmc3::SwitchBank(mmc3::window2Control, 1);
#endif
}
