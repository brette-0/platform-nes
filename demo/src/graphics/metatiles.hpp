#pragma once
#include <intsh>
#include <array>
#include "../banks.hpp"   // LEVEL_GRAPHICS
using namespace br0::intsh;

#ifdef TARGET_NES
// RAM mirrors of the five metatile planes, NOT LEVEL_GRAPHICS-tagged: the
// project's writable arena (.data/.bss) is aliased into PRG-RAM (see
// demo/link.ld's REGION_ALIAS("c_writeable", prg_ram)), which is always
// mapped -- so every read here is a plain ambient load, no MMC3 window-2
// farcall. Source of truth is still the LEVEL_GRAPHICS-banked ROM copy (see
// metatiles.cpp); LoadMetatilePlanes() below copies ROM -> RAM once per
// level load, same shape as dynamic.hpp's LoadDynamicLayer. "Graphics
// Placement Tech" (moving these into a paged bank) made every collision
// check and coin-tile fetch pay a real bank-switch farcall several times per
// actor per frame -- this trades ~1.25 KiB of PRG-RAM for that being free
// again, at the cost of one-time copy work at level load.
extern std::array<u8, 256> Metatiles_UL;   // top-left  CHR tile
extern std::array<u8, 256> Metatiles_UR;   // top-right CHR tile
extern std::array<u8, 256> Metatiles_BL;   // bottom-left  CHR tile
extern std::array<u8, 256> Metatiles_BR;   // bottom-right CHR tile
extern std::array<u8, 256> Metatiles_ATTR; // 5th plane, packed per id:
                                                 //   bits 7..2 collision class
                                                 //   bits 1..0 PPU palette index

// ROM -> RAM copy of the five planes above, from the LEVEL_GRAPHICS bank.
// Caller must already have window 2 switched to LevelGraphicsBank(n) --
// same calling convention as dynamic.hpp's LoadDynamicLayer, called from
// LoadLevel in the same style.
void LoadMetatilePlanes();
#else
// Off NES there is no MMC3 window and CREATE_SEGMENT_KEYWORD (banks.hpp's
// LEVEL_GRAPHICS) expands to nothing, so these five planes are just ordinary
// always-resident rodata already -- no bank-switch cost to work around, so no
// RAM mirror to pay for. LoadMetatilePlanes() stays a callable no-op so
// levels.cpp's LoadLevel doesn't need an #ifdef at the call site.
extern const std::array<u8, 256> Metatiles_UL;
extern const std::array<u8, 256> Metatiles_UR;
extern const std::array<u8, 256> Metatiles_BL;
extern const std::array<u8, 256> Metatiles_BR;
extern const std::array<u8, 256> Metatiles_ATTR;

inline void LoadMetatilePlanes() {}
#endif

// Layout of the 5th (attribute) byte.  Collision takes the upper 6 bits (up to
// 64 classes; we use the low values for now), palette the low 2.
inline constexpr u8 MetatileCollisionMask = 0b1111'1100;
inline constexpr u8 MetatilePaletteMask   = 0b0000'0011;

// Collision class, pre-shifted into the upper-6-bit field so a metatile's AT
// byte is simply  (MetatileCollision::Class | palette)  and a query is a
// mask-and-compare with no shifting.  enum class keeps the names out of the
// global namespace; operator| below makes the packing expression work.
enum class MetatileCollision : u8 {
    None    = 0x00,   // no collision
    Solid   = 0x04,   // blocks the actor
    Collect = 0x08,   // collectable
};

// Pack a collision class with a 2-bit palette index into one AT byte.
constexpr u8 operator|(MetatileCollision c, u8 palette) {
    return static_cast<u8>(c) | (palette & MetatilePaletteMask);
}

// Whether a collision class blocks actor movement.  Collect (coins / pickups) is
// mapped to Solid for now -- it stops the actor like a wall until the pickup
// (consume + reveal) behaviour is wired up.
constexpr bool IsBlocking(MetatileCollision c) {
    return c == MetatileCollision::Solid || c == MetatileCollision::Collect;
}

inline MetatileCollision GetMetatileCollisions(const u8 metatile) {
    return static_cast<MetatileCollision>(Metatiles_ATTR[metatile] & MetatileCollisionMask);
}