#pragma once

#include <platform-nes/technology.hpp>
#include "charmaps.hpp"   // charmap_generic must be in scope to map the string
#include "../banks.hpp"   // LEVEL_GRAPHICS

// Strings mapped through the `generic` charmap (see charmaps.hpp). Each
// character is mapped at compile time and the result is folded by the linker
// to a single std::array in rodata. Consumers just include this header; there
// is no separate definition TU. `sizeof(name)` / SIZED_OBJ(name) give the
// (unterminated) character count.
//
// LEVEL_GRAPHICS, not the plain ::STRING macro expansion -- level's own text,
// same bank as its metatiles/palette. See ::level_graphics_tag.
LEVEL_GRAPHICS inline constexpr auto msg_mary = ::tech::nes_str::encode<charmap_generic>("MARY");
