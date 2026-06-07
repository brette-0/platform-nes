#pragma once

#include "cursor.hpp"
#include <intsh>

using namespace br0::intsh;

namespace demo::level {

// Test an AABB against the level's solid metatiles.
// `origin` must already sit on the metatile containing the AABB's top-left
// corner (px, py).  w/h are the box extents in pixels.
bool CollidesSolid(const Cursor& origin, u16 px, u16 py, u8 w, u8 h);

}   // namespace demo::level
