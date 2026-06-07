#pragma once
#include "cursor.hpp"
#include "types.hpp"
#include "../types.hpp"
using namespace demo::level;

class Actor {
    public:
    Cursor cursor;
    vec2<WorldSpace> worldSpace;   // top-left origin (coarse = px, fine = subpx)
    vec2<u8> size;                 // AABB extents in pixels

    void (*start)(Actor* self);    // behaviour hooks: plain fn pointers, no vtable
    void (*update)(Actor* self);   // null for now

    void Start();
    void Update();

    void Move(vec2<i8> delta);
};

// the cursors for an NPC can be set on spawn from their metadata
// the cursors can be moved by 1/-1 when moving up/down
// the cursors can be moved by levelSize/-levelSize when moving forward/backward
// the cursors can be reset