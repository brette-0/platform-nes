#pragma once
#include <platform-nes/audio.hpp>

/**
 * @file audio_banks_unbanked.hpp
 * @brief Default definition of the audio backend's bank roles: not banked.
 *
 * audio.hpp DECLARES ::audio_code_bank_tag / ::audio_data_bank_tag but leaves
 * them undefined, because which PRG-ROM bank holds the audio engine and its
 * music data is a project's layout decision, not this library's. A C++
 * template specialisation has to be visible in the translation unit that
 * instantiates it, though, and src/nes/audio.cpp -- library code -- is that
 * translation unit. It cannot include a consuming project's own header.
 *
 * So the project injects one instead: CMakeLists.txt force-includes
 * AUDIO_BANKS_HPP into src/nes/audio.cpp (`-include`), exactly the way
 * TOPLEVEL_LD lets a project supply its own linker script. This file is the
 * fallback used when a project sets nothing -- the audio engine is ordinary
 * always-resident content, every farcall in audio.cpp collapses to a plain
 * call, and nothing is paid for banking that isn't happening.
 *
 * A project that DOES bank its audio points AUDIO_BANKS_HPP at its own
 * header (see local.cmake.example, and demo/src/banks.hpp for a worked
 * example) and defines the two roles there instead of including this.
 */

#ifdef TARGET_NES
/// Not banked: always mapped, so mmc3::CallPairedBlock runs the block
/// directly with no register writes at all.
/// @{
template <> struct mmc3::bank_layout<audio_code_bank_tag> {
    static constexpr bool always_mapped = true;
};
template <> struct mmc3::bank_layout<audio_data_bank_tag> {
    static constexpr bool always_mapped = true;
};
/// @}
#endif
