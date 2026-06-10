#pragma once
#include <platform-nes>
#include "cursor.hpp"
#include "types.hpp"
#include "../types.hpp"
using namespace demo::level;

class Actor {
    public:
    Cursor cursor;
    vec2<u16> worldSpace;          // top-left origin, sub-pixel: bottom 3 bits = subpx, so >>3 = px
    vec2<oam::oam_t> screen;       // on-screen sprite position (OAM coords)
    vec2<u8> size;                 // AABB extents in pixels
    vec2<i8> moveForce;            // subpixel movement force (8 subpx = 1px)
    i8 gravity;                    // accumulated downward accel (subpx/frame); 0 when grounded

    void (*start)(Actor* self);    // behaviour hooks: plain fn pointers, no vtable
    void (*update)(Actor* self);   // null for now

    void Start();
    void Update();

    void Move(vec2<i8> delta);
};

// Metasprite field providers: derive the sprite coordinate from the actor's
// location.  `i` is the metasprite cell index (2 wide x 4 tall: col = i&1, row = i>>1).
oam::oam_t AdjustSpriteY(Actor* self, u16 i);
oam::oam_t AdjustSpriteX(Actor* self, u16 i);

// the cursors for an NPC can be set on spawn from their metadata
// the cursors can be moved by 1/-1 when moving up/down
// the cursors can be moved by levelSize/-levelSize when moving forward/backward
// the cursors can be reset