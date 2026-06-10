#include "actor.hpp"

#include "levels.hpp"
#include "collision.hpp"
#include "../graphics/metatiles.hpp"
using namespace demo::level;

static void TranslateWorldSpace(vec2<u16>& worldSpace, const vec2<i8> delta);

// Map a (possibly out-of-bounds) sub-pixel Y to a valid metatile row.  Above the
// screen the unsigned value has underflowed (sign bit set) -> pin to the top row
// (0 = "max Y").  Below the floor -> pin to the bottom row.  (>>3 = px, >>4 = row.)
static i16 clamp_row(const u16 y) {
    if (y & 0x8000) return 0;
    const i16 r = static_cast<i16>(y >> 7);
    return r < levelHeight ? r : levelHeight - 1;
}

void Actor::Move(const vec2<i8> delta /* sub px */) {
    const auto lastWorldSpace = worldSpace;

    TranslateWorldSpace(worldSpace, delta);

    // Cursor tracks columns exactly and rows clamped to the field, so its
    // position stays consistent whether or not the actor is on-screen.
    const i16 dCol = static_cast<i16>(worldSpace.x >> 7)
                   - static_cast<i16>(lastWorldSpace.x >> 7);
    const i16 dRow = clamp_row(worldSpace.y) - clamp_row(lastWorldSpace.y);
    const i16 amt  = dCol * levelHeight + dRow;   // column-major: dCol*H + dRow

    const bool inField = !(worldSpace.y & 0x8000)
                       && (worldSpace.y >> 7) < static_cast<u16>(levelHeight);

    if (inField) {
        if (amt) cursor.Move(amt);
        const bool solid = CollidesSolid(cursor,
            worldSpace.x >> 3, worldSpace.y >> 3, size.x, size.y);
        (void)solid;   // TODO: collision response
    } else {
        // Off-field (above the screen): keep X correct for the return and pin Y
        // to the field edge, but run no collision this frame.
        if (amt) cursor.Seek(amt);
    }
}

static void TranslateWorldSpace(vec2<u16>& worldSpace, const vec2<i8> delta) {
    // delta is in sub-pixels; positions are sub-pixels too, so add directly.
    if (delta.x > 0) {
        if ((worldSpace.x + delta.x < worldSpace.x) || ((worldSpace.x + delta.x) >> 7 >= nColumns)) {
            worldSpace.x = (nColumns - 1) << 7;
        } else {
            worldSpace.x += delta.x;
        }
    } else if (delta.x < 0) {
        if (worldSpace.x + delta.x > worldSpace.x) {   // underflow past column 0
            worldSpace.x = 0;
        } else {
            worldSpace.x += delta.x;
        }
    }

    worldSpace.y += delta.y;
}

void Actor::Start() {this->start(this);}
void Actor::Update() {this->update(this);}

oam::oam_t AdjustSpriteY(Actor* self, const u16 i) {
    return static_cast<oam::oam_t>(self->screen.y + (i >> 1) * 8);
}

oam::oam_t AdjustSpriteX(Actor* self, const u16 i) {
    return static_cast<oam::oam_t>(self->screen.x + (i & 1) * 8);
}