#include "actor.hpp"

#include "levels.hpp"
#include "collision.hpp"
#include "../graphics/metatiles.hpp"
using namespace demo::level;

static void TranslateWorldSpace(vec2<WorldSpace>& worldSpace, const vec2<i8> delta);

// Map a (possibly out-of-bounds) pixel-Y to a valid metatile row.  Above the
// screen the unsigned coarse value has underflowed (sign bit set) -> pin to the
// top row (0 = "max Y").  Below the floor -> pin to the bottom row.
static i16 clamp_row(const u16 y) {
    if (y & 0x8000) return 0;
    const i16 r = static_cast<i16>(y >> 4);
    return r < levelHeight ? r : levelHeight - 1;
}

void Actor::Move(const vec2<i8> delta /* sub px */) {
    const auto lastWorldSpace = worldSpace;

    TranslateWorldSpace(worldSpace, delta);

    // Cursor tracks columns exactly and rows clamped to the field, so its
    // position stays consistent whether or not the actor is on-screen.
    const i16 dCol = static_cast<i16>(worldSpace.x.coarse >> 4)
                   - static_cast<i16>(lastWorldSpace.x.coarse >> 4);
    const i16 dRow = clamp_row(worldSpace.y.coarse) - clamp_row(lastWorldSpace.y.coarse);
    const i16 amt  = dCol * levelHeight + dRow;   // column-major: dCol*H + dRow

    const bool inField = !(worldSpace.y.coarse & 0x8000)
                       && (worldSpace.y.coarse >> 4) < static_cast<u16>(levelHeight);

    if (inField) {
        if (amt) cursor.Move(amt);
        const bool solid = CollidesSolid(cursor,
            worldSpace.x.coarse, worldSpace.y.coarse, size.x, size.y);
        (void)solid;   // TODO: collision response
    } else {
        // Off-field (above the screen): keep X correct for the return and pin Y
        // to the field edge, but run no collision this frame.
        if (amt) cursor.Seek(amt);
    }
}

static void TranslateWorldSpace(vec2<WorldSpace>& worldSpace, const vec2<i8> delta) {
    // move coarse only for now
    if (const auto diff = delta.x / 8; diff > 0) {
        if ((worldSpace.x.coarse + diff < worldSpace.x.coarse) || (worldSpace.x.coarse + diff >= (nColumns << 4))) {
            worldSpace.x.coarse = (nColumns - 1) << 4;
        } else {
            worldSpace.x.coarse += diff;
        }
    } else if (diff < 0) {
        if (worldSpace.x.coarse + diff > worldSpace.x.coarse) {
            worldSpace.x.coarse = 0;
        } else {
            worldSpace.x.coarse += diff;
        }
    }

    worldSpace.y.coarse += delta.y / 8;
}

void Actor::Start() {this->start(this);}
void Actor::Update() {this->update(this);}