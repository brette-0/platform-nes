#pragma once
#include <intsh>
#include <array>
using namespace br0::intsh;

extern const std::array<u8, 256> Metatiles_UL;   // top-left  CHR tile
extern const std::array<u8, 256> Metatiles_UR;   // top-right CHR tile
extern const std::array<u8, 256> Metatiles_BL;   // bottom-left  CHR tile
extern const std::array<u8, 256> Metatiles_BR;   // bottom-right CHR tile
extern const std::array<u8, 256> Metatiles_ATTR; // 5th plane, packed per id:
                                                 //   bits 7..2 collision class
                                                 //   bits 1..0 PPU palette index

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