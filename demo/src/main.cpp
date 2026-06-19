#include <platform-nes>
#include "main.hpp"

#include "graphics/colours.hpp"
#include "graphics/strings.hpp"
#include "graphics/graphics.hpp"
#include "graphics/metasprites.hpp"
#include "graphics/metatiles.hpp"

#include "level/levels.hpp"
#include "level/actor.hpp"
#include "level/collision_map.hpp"
#include "level/dynamic.hpp"

using namespace demo;

u8 port1;
u8 port2;

u8 lastPort1;
u8 lastPort2;

Actor player;

// ---------------------------------------------------------------------------
// Deferred VRAM tile-write stack (coin pickups)
//
// Collection runs in the UPDATE (mid-frame, rendering on), but nametable writes are
// only legal in vblank -- so a pickup pushes its tile writes here and NMI drains them.
// Each entry is {addrHi, addrLo, value}: a PRECOMPUTED VRAM address (ppu::Cartesian
// ToAddress, the (x,y)->address projection -- a /30 + %30 for the row -- paid once HERE,
// off the hot path) plus the CHR tile.  The NMI then replays each write as three register
// pokes with NO arithmetic, through the address overload of WriteSingleToNameTable (still
// the library, so SDL3 -- which has no raw PPUADDR port -- works too).  Drained by the
// CoinVramLen byte count, NOT an in-band terminator: CartesianToAddress is $2000-based on
// NES but 0-based on SDL3, where a top-of-playfield cell's high byte is legitimately 0, so
// a "high byte == 0 terminates" sentinel would falsely stop the desktop drain.
constexpr u8 kCoinVramCap = 48;            // 16 tile writes = 4 coins (2x2 each)
static u8 CoinVram[kCoinVramCap];
static u8 CoinVramLen;                      // bytes used

static void CoinVramReset() { CoinVramLen = 0; }

static void CoinVramPush(const int address, const u8 value) {
    if (CoinVramLen + 3 > kCoinVramCap) return;        // full: drop (next frame retries)
    CoinVram[CoinVramLen++] = static_cast<u8>(address >> 8);
    CoinVram[CoinVramLen++] = static_cast<u8>(address & 0xFF);
    CoinVram[CoinVramLen++] = value;
}

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
// kHudRows lives in levels.hpp now so the collision bitmap (producer) and this
// actor row projection (consumer) share one origin; reach it via level::kHudRows.
using level::kHudRows;

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
static bool PlayerBlocked(Actor* self, u16 wx, u16 wy);
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

    constexpr u8 coinUI[] = {chrHUDCoin_tile, chrFont_tile + 0, chrFont_tile + 0};
    ppu::WriteFromBufferToNameTable(video::viewport_tx() - sizeof(coinUI), 1, SIZED_OBJ(coinUI), 0);

    OAMBuffer[0] = (oam::sprite_t) {
        .y = 7,                     // accommodates for sprite rendering one scanline down.
        .tile = chrSprite0_tile,
        .attributes = 0,
        .x = 0
    };

    level::edgeR = { 0, 0, level::TileData };   // right edge walks from column 0
    level::dynEdgeR = { 0, 0 };                 // dyn forward edge, lockstep w/ edgeR
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
    level::dynEdgeL = { 0, 0 };                 // dyn backward edge, lockstep w/ edgeL

    ppu::SetScroll(0, 0);

    AudioInit();
    TrackPlay(0);

    // Seed the shared composite-metatile window over columns [0..kColMapWidth-1]
    // from the level start: a static Cursor and a dynamic Cursor both parked on
    // (col 0, row 0), composited per cell.  cameraX is 0 here, so the window is
    // already centred; ColMapTrack slides it as the camera scrolls.
    level::ColMapSeed(0, { 0, 0, level::TileData }, { 0, 0 });

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

    // Drain coin pickups collected this frame into the nametable.  Like the level-stream
    // block above, the writes touch VRAM, so disable rendering across them via SHADOW
    // (PPUMASK snapshot/restore) -- without it, a write that slips past the vblank window
    // lands mid-scanline and tears the screen.  Addresses were projected at pickup time,
    // so the body is pure register pokes ({addrHi,addrLo,value} triples).  BEFORE SetScroll:
    // these share the PPUADDR latch, which would otherwise clobber the scroll set next.
    if (CoinVramLen) SHADOW(ppu::PPUMASK) {
        ppu::PPUMASK = 0;
        for (u8 i = 0; i < CoinVramLen; i += 3)
            ppu::WriteSingleToNameTable((CoinVram[i] << 8) | CoinVram[i + 1], CoinVram[i + 2]);
    }
    CoinVramReset();

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

// Would the player's AABB at world pixel (wx, wy) be blocked?  One read of the shared
// composite-metatile window.  collectBlocks=false: coins (Collect) are pass-through
// now -- they no longer stop the player; CollectPlayerCoins picks them up on the
// committed position instead.  Only walls (Solid) block.  No RLE walk -- the cell is
// already composited in the window.  Kept under the RED profiler tint so the raster
// dump shows the per-actor collision cost (the thing that scales with actor count).
static bool PlayerBlocked(Actor* self, const u16 wx, const u16 wy) {
    ppu::SetColorPriority(ppu::mask::RED);   // TEMP profiler: the collision query
    const bool blocked = level::Blocked(wx, wy, self->size.x, self->size.y,
                                        /*collectBlocks=*/false);
    ppu::SetColorPriority(0);                // TEMP profiler: untinted everywhere else
    return blocked;
}

// Pick up any coins the player's COMMITTED AABB overlaps this frame.  CollectCoins
// does the data side (blank the run so it never returns + reveal the static cell in
// the window); here we turn each pick into its four nametable tile writes (the 2x2
// metatile, revealing the static-under tiles) and queue them for the NMI drain.  The
// nametable cell is a pure function of the world column (mod 64) + row, so no scroll
// state is needed.  STUB: score / SFX hang off here later.
static void CollectPlayerCoins(const Actor* self) {
    level::CoinPick picks[4];
    const u8 n = level::CollectCoins(PlayerWorldX(self), self->screen.y,
                                     self->size.x, self->size.y, picks, 4);
    for (u8 i = 0; i < n && i < 4; i++) {
        const u8  m  = picks[i].reveal;                  // static metatile now exposed
        const u16 tx = static_cast<u16>(picks[i].col) << 1;       // left tile column
        const u16 ty = static_cast<u16>(2 + (picks[i].row << 1)); // top tile row (HUD=2)
        // Project each of the 2x2 tiles to its VRAM address HERE (mid-frame, off the
        // vblank path).  CartesianToAddress masks X to the two-nametable window itself
        // (same mod-64 wrap the column streamer relies on), so no scroll state is needed.
        CoinVramPush(ppu::CartesianToAddress(tx,     ty),     Metatiles_UL[m]);
        CoinVramPush(ppu::CartesianToAddress(tx + 1, ty),     Metatiles_UR[m]);
        CoinVramPush(ppu::CartesianToAddress(tx,     ty + 1), Metatiles_BL[m]);
        CoinVramPush(ppu::CartesianToAddress(tx + 1, ty + 1), Metatiles_BR[m]);
    }
}

void SpriteZeroHandler() {
    ppu::SetScroll(cameraX, 16);
    spriteZeroHandled = 1;
    lastPort1 = port1; lastPort2 = port2;
    PollControllers(&port1, &port2);
    // --- TEMP raster profiler: COLLISION ONLY.  The screen is left untinted
    // (white) for everything -- audio, physics, scroll, level build, the bitmap
    // producer -- and tinted RED only across the per-actor collision queries
    // (PlayerBlocked).  So the height of the red band IS the collision cost, in
    // isolation, which is the thing that scales with actor count; level/build
    // cost is fixed and deliberately not shown.  Remove once measured.
    ppu::SetColorPriority(ppu::mask::RED | ppu::mask::GREEN);   // TEMP profiler: AudioUpdate (yellow)
    AudioUpdate();
    ppu::SetColorPriority(0);
    player.Update();
    // Keep the shared static-collision bitmap centred on the (now settled) camera.
    // Called here as a SIBLING of player.Update() -- not nested inside it -- so its
    // ZP frame reuses PlayerUpdate's freed page-zero rather than stacking on it
    // (page zero is at the cliff over the FamiStudio enclave).  Decoupled from the
    // render edge stream: it tracks cameraX directly, leaving lastXWorldSpace/edgeR
    // hysteresis untouched.
    ppu::SetColorPriority(ppu::mask::BLUE);   // TEMP profiler: window slide + ColMapStamp
    level::ColMapTrack(cameraX >> 4);
    ppu::SetColorPriority(0);
}

void PlayerUpdate(Actor* self) {
    // Grounded if a solid sits one pixel under the AABB's feet.
    bool grounded = PlayerBlocked(self, PlayerWorldX(self),
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

    // Pickup runs on the now-committed position (after the move/scroll resolve), so a
    // coin is collected exactly where the player ends the frame -- not on a rejected
    // hypothetical probe.  Queues nametable writes for the NMI drain.
    CollectPlayerCoins(self);
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

    // Vertical: accumulate the sub-pixel force into worldSpace.y; only the
    // whole-pixel carry (dy) moves the sprite / triggers collision.  This is how
    // gravity & jump keep sub-pixel precision instead of truncating each frame.
    {
        const int rawY = static_cast<int>(self->worldSpace.y) + moveForce.y;
        const i16 dy   = static_cast<i16>(rawY >> 3) - static_cast<i16>(self->worldSpace.y >> 3);
        if (dy) {
            const u16 ny = static_cast<u16>(self->screen.y) + dy;
            if (!PlayerBlocked(self, PlayerWorldX(self), ny)) {
                self->worldSpace.y = static_cast<u16>(rawY) & 0x7ff;   // 8 sub-px/px, wrap at 256px like the old u8 screen.y
                self->screen.y     = static_cast<oam::oam_t>(self->worldSpace.y >> 3);
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
    if (PlayerBlocked(self, PlayerWorldX(self) + dx, self->screen.y)) {
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

                ppu::SetColorPriority(ppu::mask::GREEN | ppu::mask::BLUE);   // TEMP profiler: render column build (cyan)
                // World metatile column the NMI will display at the right edge.
                // lastXWorldSpace is still the PRE-increment boundary here (the +=16
                // is below), so add the column we are about to advance into; this
                // matches the NMI's nametable placement (lastXWorldSpace>>4 +
                // viewport_mx after its own +=16) exactly.  The collision window
                // already holds this column, so BuildNextColumn just reads it.
                const u16 colBuiltR = ((lastXWorldSpace + 16) >> 4) + viewport_mx();
                level::BuildNextColumn(TileBuffer, colBuiltR);
                ppu::SetColorPriority(0);                                    // TEMP profiler: end render build
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
            ppu::SetColorPriority(ppu::mask::GREEN | ppu::mask::BLUE);   // TEMP profiler: render column build (cyan)
            // World metatile column entering on the left.  lastXWorldSpace is still
            // pre-decrement (the -=16 is below), so subtract the column we retreat
            // into, then -1 for the entering metatile (matches the NMI's (>>4)-1
            // placement after its own -=16).  Read straight from the window.
            const u16 colBuiltL = ((lastXWorldSpace - 16) >> 4) - 1;
            level::BuildPrevColumn(TileBuffer, colBuiltL);   // reads window column
            ppu::SetColorPriority(0);                                    // TEMP profiler: end render build
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
}

void PlayerReset(Actor* self) {
    self->gravity   = 0;
    self->moveForce = vec2<i8>{0, 0};
    playerXForce    = 0;
    playerYForce    = 0;
    playerRunTimer  = 0;
}