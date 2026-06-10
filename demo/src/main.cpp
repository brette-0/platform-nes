#include <platform-nes>
#include "main.hpp"
#include "graphics.hpp"
#include "level/levels.hpp"
#include "graphics/metasprites.hpp"
#include "colors.hpp"
#include "level/actor.hpp"
#include "level/collision.hpp"

using namespace demo;

u8 port1;
u8 port2;

u8 lastPort1;
u8 lastPort2;

Actor player;

constexpr u8 playerInitialJumpForce = 40;
u8    playerJumpForce;

i8 lastDeltaScroll;

u16 levelSize;
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

atomic u8 levelStreamCommand;
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
    ppu::Flush(0xff, 0x00);

    oam::PopulateFromProvider(OAMBuffer, 0, oam::y, Clear, 64);

    // fill in with mario metatiles
    oam::PopulateFromBuffer(OAMBuffer, 1, oam::tile, msMary, 8);
    oam::PopulateFromProvider(OAMBuffer, 1, oam::y, SpriteY, 8);
    oam::PopulateFromProvider(OAMBuffer, 1, oam::x, SpriteX, 8);

    ppu::pal::WriteFromBuffer(ppu::BG_0,         SIZED_OBJ(BGColours));
    ppu::pal::WriteFromBuffer(ppu::SPRITE_0 + 1, SIZED_OBJ(maryColors));
    ppu::WriteFromBufferToNameTable(video::viewport_tx() - sizeof(msg_mary), 0, SIZED_OBJ(msg_mary), 0);

    OAMBuffer[0] = ( oam::sprite_t){
        .y = 8,
        .tile = 0xff,
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

    if (levelStreamCommand & STREAM_LEVEL_DONE) SHADOW(ppu::PPUMASK) {
        ppu::PPUMASK = 0;
        if (levelStreamCommand & STREAM_LEVEL_RIGHT) {
            ppu::WriteFromBufferToNameTable((lastXWorldSpace >> 3) + video::viewport_tx() + 0, 2, TileBuffer, 28, 1);
            ppu::WriteFromBufferToNameTable((lastXWorldSpace >> 3) + video::viewport_tx() + 1, 2, TileBuffer + 28, 28, 1);
            if (!(levelStreamCommand & STREAM_LEVEL_SWAP))
                ppu::WriteFromBufferToAttributeTable((lastXWorldSpace >> 3) + video::viewport_tx() & ~3, 2, level::AttributeBuffer, 8, 1);
        } else {
            ppu::WriteFromBufferToNameTable((lastXWorldSpace >> 3) - 1, 2, TileBuffer, 28, 1);
            ppu::WriteFromBufferToNameTable((lastXWorldSpace >> 3) - 2, 2, TileBuffer + 28, 28, 1);
            if (!(levelStreamCommand & STREAM_LEVEL_SWAP))
                ppu::WriteFromBufferToAttributeTable((lastXWorldSpace >> 3) - 2 & ~3, 2, level::AttributeBuffer, 8, 1);
        }
    }

    ppu::SetScroll(0, 0);
    if (levelStreamCommand & STREAM_LEVEL_DONE) {
        levelStreamCommand = 0;
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

// Map a pixel-Y (possibly off-field) to a valid metatile row: above-screen
// underflow -> top row; below the floor -> bottom row.
static i16 ClampRow(const u16 y) {
    if (y & 0x8000) return 0;
    const i16 r = static_cast<i16>(y >> 4);
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
    if (port1 & ~lastPort1 & A) {   // strobe for A
        playerJumpForce = playerInitialJumpForce;
    }

    if (playerJumpForce > 0) playerJumpForce--;

    if (self->gravity < 127) self->gravity++;
    const auto inputForce = vec2 {
        static_cast<i8>(!!(port1 & LEFT) * -8 + !!(port1 & RIGHT) * 8),
        static_cast<i8>(self->gravity - playerJumpForce)
    };

    ProcessPlayerMovement(self, self->moveForce + inputForce);
}

void ProcessPlayerMovement(Actor* self, const vec2<i8> moveForce) {
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
                oam::PopulateFromProvider(OAMBuffer, 1, oam::y, SpriteY, 8);
            } else {
                self->worldSpace.y &= ~0x7;       // bonk: rest on the pixel, drop sub-px carry
                if (dy > 0) self->gravity = 0;    // landed on ground: clear accumulated fall
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
    if (dx < 0 && PlayerWorldX(self) == 0) return;
    // Horizontal: bonk (move nothing -- neither scroll nor walk) if the next
    // pixel is solid.
    if (PlayerBlocked(self, col, row, PlayerWorldX(self) + dx, self->screen.y))
        return;

    const bool couldScroll = self->screen.x == playerScrollPos
        && ((dx > 0 && cameraX + dx <= (level::nColumns - viewport_mx()) << 4)
            | (dx < 0 && cameraX > 0));

    if (couldScroll) {
        if (dx > 0) cameraX = cameraX + 1;
        else        cameraX = cameraX - 1;

        if (!(cameraX & 0x0f)) {
            if (dx > 0) {
                if (cameraX > lastXWorldSpace && cameraX != (level::nColumns - viewport_mx()) << 4) {
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
                    lastDeltaScroll = static_cast<i8>(dx);
                    lastXWorldSpace = cameraX;
                    levelStreamCommand = levelStreamCommand | STREAM_LEVEL_DONE;
                }
            } else if (cameraX == lastXWorldSpace - 16 && cameraX != 0) {
                // No column exists left of column 0.  Firing here would walk
                // edgeL/edgeR backward past offset 0 (Cursor::Move has no floor),
                // wrapping offset to 0xFFFF and fetching adjacent ROM as bogus
                // metatile ids -- real-looking tiles streamed to the wrong place
                // (the left-edge "displacement"/"split ground"), and the corrupt
                // cursor then poisons every later stream.  col 0 itself is still
                // revealed normally at cameraX==16, so nothing is lost.
                levelStreamCommand = STREAM_LEVEL_LEFT;
                if (lastDeltaScroll > 0)
                    levelStreamCommand = levelStreamCommand | STREAM_LEVEL_SWAP;

                lastDeltaScroll = static_cast<i8>(dx);
                lastXWorldSpace = cameraX;
                level::BuildPrevColumn(TileBuffer);   // walks edgeL back one column
                // The right edge retreated one column too; keep edgeR parked there.
                level::edgeR.Move(-level::levelHeight);
                edgeRAbs -= level::levelHeight;
                levelStreamCommand = levelStreamCommand | STREAM_LEVEL_DONE;
            }
        }
    } else {
        const oam::oam_t lastPlayerX = self->screen.x;
        self->screen.x += dx;
        if (dx > 0) {
            if (self->screen.x > playerMaxXPos) {
                self->screen.x = playerMaxXPos;
            }
        } else {
            if (self->screen.x > lastPlayerX) {
                self->screen.x = 0;
            }
        }

        oam::PopulateFromProvider(OAMBuffer, 1, oam::x, SpriteX, 8);
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

    const i16 col = static_cast<i16>(PlayerWorldX(self) >> 4);
    const i16 row = ClampRow(self->screen.y);

    if (const i16 amt = col * level::levelHeight + row) self->cursor.Move(amt);
}