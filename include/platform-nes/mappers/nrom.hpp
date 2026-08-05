/**
 * @file nrom.hpp
 * @brief NROM (mapper 0): fixed CHR-ROM, no bank switching at all.
 *
 * NROM has no bank-select registers of any kind -- CHR-ROM (if present; some
 * NROM boards use CHR-RAM instead) is a single fixed 8 KiB image mapped
 * straight onto the PPU's $0000-$1FFF, so a tile's PPU pattern-table address
 * already IS its offset into that image. ::NROM::GetTileLMA exists purely so
 * the emu PPU (src/emu/ppu.cpp) always has a valid ::ppu::TileTranslator
 * bound -- games that never call ::ppu::BindTileTranslator (i.e. don't use a
 * bank-switching mapper) get this identity translator by default.
 *
 * Same non-instantiable, all-static shape as ::mmc3 for consistency, though
 * NROM has no registers or hardware ports to warrant a class on the NES side
 * -- this header is emu-relevant only.
 */
#pragma once

#include <intsh>
using namespace br0::intsh;

class NROM {
public:
    NROM() = delete;
    NROM(const NROM &) = delete;
    NROM &operator=(const NROM &) = delete;

    /**
     * @brief Identity tile-address translator.
     *
     * NROM has no CHR bank switching, so a tile's logical PPU pattern-table
     * address is already its physical CHR-ROM offset -- this returns @p
     * tileVMA unchanged. Matches ::ppu::TileTranslator's signature.
     */
    static u32 GetTileLMA(const u16 tileVMA) { return tileVMA; }
};
