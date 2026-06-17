#pragma once
#include <platform-nes/video.hpp>

extern const oam::sprite_t msMary[0x04];

// Hardware-sprite count of the player metasprite (16x16 = 2x2), derived from
// msMary's array extent so the OAM populate counts can never drift from the
// metasprite's actual size. A compile-time constant: folds to a literal with
// zero runtime cost.
inline constexpr u8 kMarySprites = static_cast<u8>(sizeof(msMary) / sizeof(msMary[0]));