#include "collision.hpp"

#include "levels.hpp"
#include "../graphics/metatiles.hpp"

using namespace demo::level;

// A 16px metatile boundary every (px >> 4).  An AABB spans the columns
// [px>>4 .. (px+w-1)>>4] and rows [py>>4 .. (py+h-1)>>4] -- at most 2x2 for a
// <=16px actor.  We probe each overlapped cell from a *copy* of the origin
// cursor so the actor's own cursor is left untouched.
bool demo::level::CollidesSolid(const Cursor& origin, u16 px, u16 py, u8 w, u8 h) {
    // Vertical level region is rows [0, levelHeight).  If the top-left row is
    // outside it -- above the screen (py underflowed to a huge value) or below
    // the level -- there is no tilemap here, so skip collision entirely.
    const u16 rowTop = py >> 4;
    if (rowTop >= levelHeight) return false;

    // Clamp the AABB's bottom row to the level floor so a probe never walks
    // across the column-major seam into the next column's data.
    u16 rowBot = (py + h - 1) >> 4;
    if (rowBot >= levelHeight) rowBot = levelHeight - 1;

    const u8 cols = ((px + w - 1) >> 4) - (px >> 4);
    const u8 rows = static_cast<u8>(rowBot - rowTop);

    for (u8 c = 0; c <= cols; c++) {
        for (u8 r = 0; r <= rows; r++) {
            Cursor probe = origin;
            probe.Move(c * levelHeight + r);   // column-major: dCol*H + dRow
            if (GetMetatileCollisions(probe.Fetch()) == Solid)
                return true;
        }
    }
    return false;
}
