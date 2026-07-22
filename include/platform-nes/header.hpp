/**
 * @file header.hpp
 * @brief Emits the 16-byte NES2.0 ROM header (https://www.nesdev.org/wiki/NES_2.0).
 */
#pragma once

#include <intsh>
using namespace br0::intsh;

#ifdef TARGET_NES

/**
 * @brief Places the 16 raw NES2.0 header bytes at the front of the ROM file.
 *
 * Call exactly once, in exactly one TU, with the 16 header bytes in file
 * order (identifier x4, PRG-ROM size, CHR-ROM size, flags 6-15 per the
 * NES2.0 spec). The bytes land in the `.nes2_header` section, which the
 * mapper's linker script (e.g. vrc1.ld) places via its `nes2_header` MEMORY
 * region ahead of PRG-ROM/CHR-ROM, replacing the old `INCLUDE
 * ines-header.ld` mechanism.
 *
 * TODO: make this pretty -- named fields (program ROM size, character ROM
 * size, mapper, mirroring, battery, submapper, timing, ...) each computed
 * from a single constexpr expression with static_asserts on their ranges,
 * instead of 16 unchecked positional bytes the caller has to hand-compute.
 */
#define NES2_HEADER(b0, b1, b2, b3, b4, b5, b6, b7, b8, b9, b10, b11, b12, b13, b14, b15) \
    extern "C" [[gnu::section(".nes2_header"), gnu::used, gnu::retain]]                  \
    constexpr u8 _nes2_header[16] = {                                                    \
        (b0), (b1), (b2), (b3), (b4), (b5), (b6), (b7),                                  \
        (b8), (b9), (b10), (b11), (b12), (b13), (b14), (b15)                             \
    }

#else
/** @brief Off-NES builds produce no ROM file, hence no header to emit. */
#define NES2_HEADER(...)
#endif
