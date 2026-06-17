#include <platform-nes>
#include "main.hpp"

#include "graphics/colours.hpp"
#include "graphics/strings.hpp"
#include "graphics/graphics.hpp"
#include "graphics/metasprites.hpp"
#include "graphics/metatiles.hpp"

#include "level/levels.hpp"
#include "level/actor.hpp"
#include "level/collision.hpp"

using namespace demo;

u8 port1;
u8 port2;

u8 lastPort1;
u8 lastPort2;

Actor player;

// Horizontal velocity caps (subpixel/frame; 8 subpx = 1px).
constexpr i8  kMaxWalk     = 12;   // 1.5 px/frame
constexpr i8  kMaxRun      = 20;   // 2.5 px/frame
// Per-frame acceleration, accumulated in 1/256-subpixel so the ramp is gradual.
constexpr i16 kWalkAccel   = 76;
constexpr i16 kRunAccel    = 112;
constexpr i16 kFriction    = 76;   // coast-down when grounded with no input
constexpr i16 kSkid        = 152;  // turnaround deceleration when grounded
constexpr i16 kAirAccel    = 76;   // limited control while airborne, no friction
constexpr u8  kRunRetain   = 10;   // frames the run cap persists after releasing B

// Vertical velocity (subpixel/frame) and gravity (1/256-subpixel/frame).
constexpr i8  kJumpInit    = 32;   // launch speed off the ground
constexpr i8  kRunJumpInit = 36;   // taller launch at running speed
constexpr i16 kRiseGravity = 256;  // ascending with A held -> floaty
constexpr i16 kFallGravity = 896;  // descending or A released -> heavier
constexpr i8  kMaxFall     = 36;   // 4.5 px/frame terminal

// The sprite-0 split scrolls the playfield down one metatile (SetScroll(.,16))
// and level columns are written one metatile below the top (nametable row 2),
// so on screen level-data row 0 sits a HUD row below worldSpace.y 0. World-row 0
// is the HUD strip itself -- a real part of world space, just with no level data.
constexpr i16 kHudRows     = 1;    // metatile rows of HUD above level-data row 0

i16 playerXForce;   // horizontal acceleration remainder (1/256-subpixel/frame)
i16 playerYForce;   // gravity remainder (1/256-subpixel/frame)
u8  playerRunTimer;

i8 lastDeltaScroll;

u16 levelSize;

// World-pixel boundary of the most recently streamed column (always a multiple
// of 16).  Doubles as the scroll hysteresis marker: a new column is only built
// once the camera has travelled a full 16px column past it, so jitter or a
// direction reversal near a boundary can't rapidly toggle BuildNext/BuildPrev
// and drift the edge cursors.  Read by the NMI to place the nametable write.
atomic u16 lastXWorldSpace;

// Right edge's absolute metatile offset from the level start. Tracked so the
// left edge can be parked exactly kEdgeGap metatiles behind it without an O(n)
// re-walk: edgeL's target is derived from this counter, not from a one-shot seek.
u16 edgeRAbs;

// Camera scroll origin in *pixels* (left edge of the viewport in world space).
// The actor's sub-pixel worldSpace is canonical; this is derived/maintained from
// it each frame because the PPU scroll register is integer-pixel only.
u16 cameraX;

atomic u8 spriteZeroHandled;

oam::sprite_t OAMBuffer[64] __attribute__((aligned(256)));

atomic enum_flags<eLevelStreamCommands> levelStreamCommand;
u8 TileBuffer[56];

static oam::oam_t Clear(u16 _);
static void PlayerUpdate(Actor* self);
static void PlayerReset(Actor* self);
static oam::oam_t SpriteY(u16 i);
static oam::oam_t SpriteX(u16 i);
static u16  PlayerWorldX(const Actor* self);
static i16  ClampRow(u16 y);
static bool PlayerBlocked(const Actor* self, i16 col, i16 row, u16 wx, u16 wy);
static void ProcessPlayerMovement(Actor* self, vec2<i8> moveForce);

RESET {
    if (!level::LoadLevel(0)) {
        reset();    // spin reset on NES, exit on SDL3
    }
    ppu::Flush(chrHUDWhitespace_tile, 0x11);

    oam::PopulateFromProvider(OAMBuffer, 0, oam::y, Clear, 64);

    // fill in with mario metatiles
    oam::PopulateFromBuffer(  OAMBuffer, 1, oam::tile, msMary, kMarySprites);
    oam::PopulateFromProvider(OAMBuffer, 1, oam::y, SpriteY, kMarySprites);
    oam::PopulateFromProvider(OAMBuffer, 1, oam::x, SpriteX, kMarySprites);

    ppu::pal::WriteFromBuffer(ppu::BG_0,         SIZED_OBJ(BGColours));
    ppu::pal::WriteFromBuffer(ppu::SPRITE_0 + 1, SIZED_OBJ(maryColors));
    ppu::WriteFromBufferToNameTable(video::viewport_tx() - sizeof(msg_mary), 0, SIZED_OBJ(msg_mary), 0);

    OAMBuffer[0] = (oam::sprite_t) {
        .y = 8,
        .tile = chrSprite0_tile,
        .attributes = 0,
        .x = 0
    };

    level::edgeR = { 0, 0, level::TileData };   // right edge walks from column 0
    for (auto i = 0; i < 2 + video::viewport_tx(); i += 2) {
        ppu::WriteFromProviderToNameTable(
            i, 2,
            level::GetNextWrite, 28, 1
        );

        ppu::WriteFromProviderToNameTable(
            i + 1, 2,
            level::GetCurrentNext, 28, 1
        );

        ppu::WriteFromBufferToAttributeTable(i & ~3, 2, level::AttributeBuffer, 8, 1);
    }

    // Seed the left edge at reset so it is valid from the first frame -- no lazy
    // first-left-stream re-walk spike.  The fill above leaves edgeR 17 columns in
    // (metatile (1+viewport_mx())*levelHeight), still short of the steady-state
    // kEdgeGap, so edgeL clamps to the level start and closes the startup gap over
    // the first couple of right-streams -- each step is the same +levelHeight Move
    // it always does, so there is no extra cost and never an O(n) walk.
    edgeRAbs     = (1 + viewport_mx()) * level::levelHeight;
    level::edgeL = { 0, 0, level::TileData };

    ppu::SetScroll(0, 0);

    AudioInit();
    TrackPlay(0);

    player.cursor.base     = TileData;
    player.cursor.offset   = 0;
    player.cursor.progress = 0;

    player.size = {
        16, 16
    };

    player.start  = PlayerReset;
    player.update = PlayerUpdate;

    player.Start();
    oam::RefreshSprites(OAMBuffer);   /* seed the first frame's sprite snapshot */
    ppu::EnableRendering(ppu::ctrl::BG_ADDR, ppu::mask::BG_L | ppu::mask::SPRITE_L);
    // ReSharper disable once CppDFAEndlessLoop
    while (!quit) {
        if (port1 & START) {
#ifndef  TARGET_NES
            quit = 1;
#endif

        }

        WaitThenReactToSpriteZero(0, 16, SpriteZeroHandler, &spriteZeroHandled);

        video::WaitForPresent();
    }
}

NMI {
    ppu::SetColorPriority(ppu::mask::BLUE);
    oam::RefreshSprites(OAMBuffer);

    spriteZeroHandled = 0;

    using enum eLevelStreamCommands;
    if (levelStreamCommand & eLevelStreamCommands::STREAM_LEVEL_DONE) SHADOW(ppu::PPUMASK) {
        ppu::PPUMASK = 0;
        if (levelStreamCommand & eLevelStreamCommands::STREAM_LEVEL_RIGHT) {
            ppu::WriteFromBufferToNameTable((lastXWorldSpace >> 3) + video::viewport_tx() + 0, 2, TileBuffer, 28, 1);
            ppu::WriteFromBufferToNameTable((lastXWorldSpace >> 3) + video::viewport_tx() + 1, 2, TileBuffer + 28, 28, 1);
            if (!(levelStreamCommand & eLevelStreamCommands::STREAM_LEVEL_SWAP))
                ppu::WriteFromBufferToAttributeTable((lastXWorldSpace >> 3) + video::viewport_tx() & ~3, 2, level::AttributeBuffer, 8, 1);
        } else {
            ppu::WriteFromBufferToNameTable((lastXWorldSpace >> 3) - 1, 2, TileBuffer, 28, 1);
            ppu::WriteFromBufferToNameTable((lastXWorldSpace >> 3) - 2, 2, TileBuffer + 28, 28, 1);
            if (!(levelStreamCommand & eLevelStreamCommands::STREAM_LEVEL_SWAP))
                ppu::WriteFromBufferToAttributeTable((lastXWorldSpace >> 3) - 2 & ~3, 2, level::AttributeBuffer, 8, 1);
        }
    }

    ppu::SetScroll(0, 0);
    if (levelStreamCommand & STREAM_LEVEL_DONE) {
        levelStreamCommand = {};
    }

    ppu::SetColorPriority(0);
}

static oam::oam_t Clear(const u16 _) {
    return 0xef;
}

// Provider shims: oam::PopulateFromProvider hands the callback only the sprite
// index, so bind the player Actor here and forward to the Actor-aware versions.
static oam::oam_t SpriteY(const u16 i) { return AdjustSpriteY(&player, i); }
static oam::oam_t SpriteX(const u16 i) { return AdjustSpriteX(&player, i); }

// Player world-space pixel X: the actor's sub-pixel worldSpace.x dropped to px.
// (camera + screen.x are derived from it; this is the single source of truth.)
static u16 PlayerWorldX(const Actor* self) {
    return static_cast<u16>(self->worldSpace.x >> 3);
}

static i8 AbsI8(const i8 v) { return v < 0 ? static_cast<i8>(-v) : v; }

// Map a pixel-Y to the LEVEL-DATA metatile row it overlaps. World-row 0 is the
// HUD strip (kHudRows tall, scrolled in by the sprite-0 split), so level-data
// row 0 starts kHudRows below the top. Subtracting it here -- where the row
// projection lives -- keeps every caller (seed, grounded check, vertical move)
// indexing the scrolled background consistently, and unlike a one-shot cursor
// seed it can't be swallowed at the level start: the offset simply develops as
// the actor descends into the field. Above the field (underflow) or inside the
// HUD clamps to the top row; below the floor clamps to the bottom.
static i16 ClampRow(const u16 y) {
    if (y & 0x8000) return 0;
    const i16 r = static_cast<i16>(y >> 4) - kHudRows;
    if (r < 0) return 0;
    return r < level::levelHeight ? r : level::levelHeight - 1;
}

// Would the player's AABB overlap a solid metatile at world pixel (wx, wy)?
// (col,row) is the metatile the live cursor currently sits on; we probe from a
// copy moved to the prospective cell so the actor's own cursor is left alone.
static bool PlayerBlocked(const Actor* self, const i16 col, const i16 row, const u16 wx, const u16 wy) {
    const i16 tCol = static_cast<i16>(wx >> 4);
    const i16 tRow = ClampRow(wy);
    level::Cursor probe = self->cursor;
    probe.Move((tCol - col) * level::levelHeight + (tRow - row));
    return level::CollidesSolid(probe, wx, wy, self->size.x, self->size.y);
}

void SpriteZeroHandler() {
    ppu::SetScroll(cameraX, 16);
    spriteZeroHandled = 1;
    lastPort1 = port1; lastPort2 = port2;
    PollControllers(&port1, &port2);
    AudioUpdate();
    player.Update();
}

void PlayerUpdate(Actor* self) {
    // Grounded if a solid sits one pixel under the AABB's feet.
    const i16 col = static_cast<i16>(PlayerWorldX(self) >> 4);
    const i16 row = ClampRow(self->screen.y);
    bool grounded = PlayerBlocked(self, col, row, PlayerWorldX(self),
                                  static_cast<u16>(self->screen.y + 1));

    const bool left  = port1 & LEFT;
    const bool right = port1 & RIGHT;
    const i8   dir   = (right && !left) ? 1 : ((left && !right) ? -1 : 0);

    // -- horizontal: build speed toward a cap, shed it via friction/skid --
    const i8 vx0 = self->moveForce.x;
    i8       vx  = vx0;

    if ((port1 & B) && grounded && dir != 0 && (vx == 0 || (vx > 0) == (dir > 0)))
        playerRunTimer = kRunRetain;
    else if (playerRunTimer)
        playerRunTimer--;
    const i8 maxSpeed = playerRunTimer ? kMaxRun : kMaxWalk;

    if (dir != 0) {
        const bool sameDir = vx == 0 || (vx > 0) == (dir > 0);
        if (!sameDir)
            playerXForce += dir * (grounded ? kSkid : kAirAccel);
        else if (AbsI8(vx) < maxSpeed)
            playerXForce += dir * (grounded ? (playerRunTimer ? kRunAccel : kWalkAccel) : kAirAccel);
        else if (AbsI8(vx) > maxSpeed && grounded)
            playerXForce -= dir * kFriction;
    } else if (grounded && vx != 0) {
        playerXForce += vx > 0 ? -kFriction : kFriction;
    }

    while (playerXForce >= 256)  { playerXForce -= 256; if (vx < kMaxRun)  vx++; }
    while (playerXForce <= -256) { playerXForce += 256; if (vx > -kMaxRun) vx--; }

    // Friction must settle to rest, not creep into the opposite direction.
    if (dir == 0 && grounded && vx0 != 0 && (vx0 > 0) != (vx > 0)) {
        vx = 0;
        playerXForce = 0;
    }
    self->moveForce.x = vx;

    // -- vertical: launch on A, then variable gravity until something stops us --
    if (grounded && (port1 & ~lastPort1 & A)) {
        self->moveForce.y = static_cast<i8>(-(AbsI8(vx) >= kMaxWalk ? kRunJumpInit : kJumpInit));
        playerYForce = 0;
        grounded = false;
    }
    if (grounded) {
        self->moveForce.y = 0;
        playerYForce = 0;
    }

    const bool rising = self->moveForce.y < 0;
    playerYForce += (rising && (port1 & A)) ? kRiseGravity : kFallGravity;
    i8 vy = self->moveForce.y;
    while (playerYForce >= 256) { playerYForce -= 256; if (vy < kMaxFall) vy++; }
    self->moveForce.y = vy;

    ProcessPlayerMovement(self, self->moveForce);
}

void ProcessPlayerMovement(Actor* self, const vec2<i8> moveForce) {
    using enum eLevelStreamCommands;
    // Recomputed per call: on NES these fold to literals (-O3); on desktop the
    // viewport is a runtime value, so they must be read after video init, not
    // bound to a constexpr / static-init global.
    // ReSharper disable once CppVariableCanBeMadeConstexpr

    const auto playerScrollPos = (video::viewport_px() >> 1);
    // ReSharper disable once CppVariableCanBeMadeConstexpr
    const auto playerMaxXPos   = (video::viewport_px() - 16);

    // The cursor sits on the metatile under the actor's top-left corner; (col,
    // row) mirror that cell so we can probe and re-sync it as the actor moves.
    i16 col = static_cast<i16>(PlayerWorldX(self) >> 4);
    i16 row = ClampRow(self->screen.y);

    // Vertical: accumulate the sub-pixel force into worldSpace.y; only the
    // whole-pixel carry (dy) moves the sprite / triggers collision.  This is how
    // gravity & jump keep sub-pixel precision instead of truncating each frame.
    {
        const int rawY = static_cast<int>(self->worldSpace.y) + moveForce.y;
        const i16 dy   = static_cast<i16>(rawY >> 3) - static_cast<i16>(self->worldSpace.y >> 3);
        if (dy) {
            const u16 ny = static_cast<u16>(self->screen.y) + dy;
            if (!PlayerBlocked(self, col, row, PlayerWorldX(self), ny)) {
                self->worldSpace.y = static_cast<u16>(rawY) & 0x7ff;   // 8 sub-px/px, wrap at 256px like the old u8 screen.y
                self->screen.y     = static_cast<oam::oam_t>(self->worldSpace.y >> 3);
                const i16 nrow = ClampRow(self->screen.y);
                self->cursor.Move(nrow - row);   // same column, row delta only
                row = nrow;
                oam::PopulateFromProvider(OAMBuffer, 1, oam::y, SpriteY, kMarySprites);
            } else {
                self->worldSpace.y &= ~0x7;       // bonk: rest on the pixel, drop sub-px carry
                self->moveForce.y = 0;            // land or head-bonk both kill vertical speed
            }
        } else {
            self->worldSpace.y = static_cast<u16>(rawY) & 0x7ff;       // sub-pixel only, no pixel move
        }
    }

    if (!moveForce.x) return;    // if no need to move player sprite x or sroll, return early

    // Accumulate the sub-pixel X force; dx is the whole-pixel carry this frame.
    const int rawX = static_cast<int>(self->worldSpace.x) + moveForce.x;
    const i16 dx   = static_cast<i16>(rawX >> 3) - static_cast<i16>(self->worldSpace.x >> 3);
    if (!dx) {
        if (rawX >= 0) self->worldSpace.x = static_cast<u16>(rawX);   // sub-pixel only
        return;
    }

    // Left world edge: nothing exists left of column 0, so it's a hard wall.
    // (Also keeps the next line's world X from underflowing u16 -> a wild
    // collision probe that walks thousands of runs off the level data.)
    if (dx < 0 && PlayerWorldX(self) == 0) { self->moveForce.x = 0; return; }
    // Horizontal: bonk (move nothing -- neither scroll nor walk) if the next
    // pixel is solid.  Kill the speed so it must rebuild after the wall clears.
    if (PlayerBlocked(self, col, row, PlayerWorldX(self) + dx, self->screen.y)) {
        self->moveForce.x = 0;
        return;
    }

    // Pin the player to the viewport midpoint by absorbing off-centre motion into
    // camera scroll; the player only leaves the midpoint when the camera is
    // clamped at a level edge.  Exact-centre equality used to gate scrolling, but
    // a large dx steps over the midpoint pixel and strands the player at the
    // screen edge -- so split this frame's motion: walk up to the midpoint and
    // scroll the remainder (pulling the player back when it overshoots).  sx is
    // computed in i16 to survive the off-screen over/underflow before clamping.
    const u16 maxCam = (level::nColumns - viewport_mx()) << 4;
    i16       sx     = static_cast<i16>(self->screen.x) + dx;
    const i16 excess = sx - static_cast<i16>(playerScrollPos);   // signed: +right, -left

    i16 scroll = 0;
    if (excess > 0) {                                      // right of centre -> scroll right
        const u16 room = cameraX < maxCam ? maxCam - cameraX : 0;
        scroll = static_cast<u16>(excess) <= room ? excess : static_cast<i16>(room);
    } else if (excess < 0) {                               // left of centre -> scroll left
        const u16 want = static_cast<u16>(-excess);
        scroll = want <= cameraX ? excess : -static_cast<i16>(cameraX);
    }

    if (scroll) {
        cameraX = static_cast<u16>(static_cast<i16>(cameraX) + scroll);
        sx     -= scroll;                                  // re-centre by the absorbed amount

        // Stream against lastXWorldSpace (the last built column's boundary), not
        // against an exact-boundary landing: a build fires only once the camera
        // has travelled a full 16px column *past* the last build.  That dead zone
        // is the hysteresis -- without it, jitter or a reversal just past a
        // boundary would toggle BuildNext/BuildPrev every frame and the
        // asymmetric edge bookkeeping below would drift the level vs the camera.
        // |scroll| <= 16 plus the dead zone => at most one boundary per frame, so
        // a single (non-looping) build per frame is sufficient.
        if (scroll > 0) {
            if (cameraX >= lastXWorldSpace + 16 && lastXWorldSpace + 16 != maxCam) {
                levelStreamCommand = STREAM_LEVEL_RIGHT;
                // SWAP only suppresses the NMI attribute write on a reversal;
                // edgeR is already parked at the right edge, so no re-walk.
                if (lastDeltaScroll < 0)
                    levelStreamCommand = levelStreamCommand | STREAM_LEVEL_SWAP;

                level::BuildNextColumn(TileBuffer);
                // Keep the left edge parked kEdgeGap metatiles behind the right
                // edge.  Past the startup gap both advance one full column per
                // right-stream; before then the clamp holds edgeL at the level
                // start and it closes the gap by 13 then 14 over two streams.
                const u16 kEdgeGap   = (viewport_mx() + 2) * level::levelHeight + 1;
                const u16 oldTarget  = edgeRAbs > kEdgeGap ? edgeRAbs - kEdgeGap : 0;
                edgeRAbs += level::levelHeight;
                const u16 newTarget  = edgeRAbs > kEdgeGap ? edgeRAbs - kEdgeGap : 0;
                level::edgeL.Move(static_cast<i16>(newTarget - oldTarget));
                lastDeltaScroll = static_cast<i8>(scroll);
                lastXWorldSpace += 16;
                levelStreamCommand = levelStreamCommand | STREAM_LEVEL_DONE;
            }
        } else if (cameraX <= lastXWorldSpace - 16 && lastXWorldSpace != 16) {
            // No column exists left of column 0.  Firing at boundary 0 would walk
            // edgeL/edgeR backward past offset 0 (Cursor::Move has no floor),
            // wrapping offset to 0xFFFF and fetching adjacent ROM as bogus
            // metatile ids -- real-looking tiles streamed to the wrong place
            // (the left-edge "displacement"/"split ground"), and the corrupt
            // cursor then poisons every later stream.  col 0 itself is still
            // revealed normally at cameraX==16, so nothing is lost.
            levelStreamCommand = STREAM_LEVEL_LEFT;
            if (lastDeltaScroll > 0)
                levelStreamCommand = levelStreamCommand | STREAM_LEVEL_SWAP;

            lastDeltaScroll = static_cast<i8>(scroll);
            level::BuildPrevColumn(TileBuffer);   // walks edgeL back one column
            // The right edge retreated one column too; keep edgeR parked there.
            level::edgeR.Move(-level::levelHeight);
            edgeRAbs -= level::levelHeight;
            lastXWorldSpace -= 16;
            levelStreamCommand = levelStreamCommand | STREAM_LEVEL_DONE;
        }
    }

    // Clamp the residual on-screen position (the part of dx not absorbed into
    // scroll) and refresh the sprite only when it actually moved on screen.
    if (sx < 0) sx = 0;
    else if (sx > static_cast<i16>(playerMaxXPos)) sx = static_cast<i16>(playerMaxXPos);
    if (static_cast<oam::oam_t>(sx) != self->screen.x) {
        self->screen.x = static_cast<oam::oam_t>(sx);
        oam::PopulateFromProvider(OAMBuffer, 1, oam::x, SpriteX, kMarySprites);
    }

    // Recompose the canonical sub-pixel X from the (possibly clamped) camera +
    // screen pixel position, keeping the freshly accumulated sub-pixel remainder.
    self->worldSpace.x = static_cast<u16>(((cameraX + self->screen.x) << 3) | (static_cast<u16>(rawX) & 7));

    // Re-sync the cursor's column to the actor's new world column (a screen-edge
    // clamp can leave the column unchanged even when deltaX was non-zero).
    const i16 ncol = static_cast<i16>(PlayerWorldX(self) >> 4);
    if (ncol != col) self->cursor.Move((ncol - col) * level::levelHeight);
}

void PlayerReset(Actor* self) {
    // Stream the cursor from the level start, then walk it to the actor's
    // current metatile.  Column-major: index = col*levelHeight + row.
    self->cursor.base     = TileData;
    self->cursor.offset   = 0;
    self->cursor.progress = 0;
    self->gravity         = 0;
    self->moveForce       = vec2<i8>{0, 0};
    playerXForce          = 0;
    playerYForce          = 0;
    playerRunTimer        = 0;

    const i16 col = static_cast<i16>(PlayerWorldX(self) >> 4);
    const i16 row = ClampRow(self->screen.y);

    if (const i16 amt = col * level::levelHeight + row) self->cursor.Move(amt);
}