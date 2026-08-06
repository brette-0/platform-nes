#include "actor.hpp"

#include "level/levels.hpp"
#include "level/collision_map.hpp"

static void TranslateWorldSpace(vec2<u16>& worldSpace, vec2<i8> delta);

void Actor::Move(const vec2<i8> delta /* sub px */) {
    TranslateWorldSpace(worldSpace, delta);

    const bool inField = !(worldSpace.y & 0x8000)
                       && (worldSpace.y >> 7) < static_cast<u16>(demo::level::levelHeight);

    if (inField) {
        // Block test against the shared composite-metatile window.  Enemies pass
        // collectBlocks=false: coins (Collect class) are walked through, only walls
        // (Solid) stop them.  No per-actor RLE walk -- Blocked reads the camera-centred
        // window resolved once per scroll.  World-pixel inputs; the HUD strip is removed
        // internally.
        const bool solid = demo::level::Blocked(worldSpace.x >> 3, worldSpace.y >> 3,
                                                GetSize(), /*collectBlocks=*/false);
        (void)solid;   // TODO: collision response
    }
    // Off-field (above the screen): no collision runs this frame.
}

static void TranslateWorldSpace(vec2<u16>& worldSpace, const vec2<i8> delta) {
    // delta is in sub-pixels; positions are sub-pixels too, so add directly.
    if (delta.x > 0) {
        if ((worldSpace.x + delta.x < worldSpace.x) || ((worldSpace.x + delta.x) >> 7 >= demo::level::nColumns)) {
            worldSpace.x = (demo::level::nColumns - 1) << 7;
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

oam::oam_t AdjustSpriteY(const Actor* self, const u16) {
    return static_cast<oam::oam_t>(self->screen.y);
}

oam::oam_t AdjustSpriteX(const Actor* self, const u16 i) {
    return static_cast<oam::oam_t>(self->screen.x + (i & 1) * 8);
}