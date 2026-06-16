// Anchor TU for the charmaps. A CHARMAP is a constexpr (inline) function with no
// out-of-line definition to emit, so this TU intentionally produces nothing --
// the charmap is used wherever charmaps.hpp is included (e.g. graphics/strings.cpp).
#include "charmaps.hpp"
