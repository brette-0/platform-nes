#pragma once

#include <platform-nes/technology.hpp>
#include "charmaps.hpp"   // charmap_generic must be in scope to map the string

// Strings mapped through the `generic` charmap (see charmaps.hpp). STRING is a
// header-only `inline constexpr` definition: each character is mapped at compile
// time and the result is folded by the linker to a single std::array in rodata.
// Consumers just include this header; there is no separate definition TU.
// `sizeof(name)` / SIZED_OBJ(name) give the (unterminated) character count.
STRING(generic, msg_mary, MARY);
