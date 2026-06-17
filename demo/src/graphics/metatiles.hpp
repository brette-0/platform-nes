#pragma once
#include <intsh>
#include <array>
using namespace br0::intsh;

// Four CHR-tile planes indexed by metatile id (0..255), split at compile time
// from a single interleaved { UL, UR, BL, BR } source in metatiles.cpp. Each
// lookup is a one-instruction `lda Metatiles_xx,x` (no 16-bit index math).
extern const std::array<u8, 256> Metatiles_UL;   // top-left  CHR tile
extern const std::array<u8, 256> Metatiles_UR;   // top-right CHR tile
extern const std::array<u8, 256> Metatiles_BL;   // bottom-left  CHR tile
extern const std::array<u8, 256> Metatiles_BR;   // bottom-right CHR tile
extern const std::array<u8, 256> Metatiles_ATTR; // 5th plane: 2-bit palette index per id
extern const u8 MetatileCollisions[];

enum eMetatiles : u8 {
    mtTerrain       = 0x3c,
    mtCoin          = 0x62,
    mtAir           = 0xfe,
    mtHUDWhiteSpace = 0xff
};

enum class EMetatileCollisions : u8 {
    Clear,
    Solid
};

EMetatileCollisions GetMetatileCollisions(u8 metatile);