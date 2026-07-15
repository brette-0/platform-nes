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
#include "level/player.hpp"

#include "technology.hpp"
#include <platform-nes/apu.hpp>
#include <platform-nes/mappers/vrc1.hpp>

using namespace demo;

volatile bool nmi_done;

u8 port1;
u8 port2;

u8 lastPort1;
u8 lastPort2;

// Forward-declared so NMI can call it directly once the real sp
// rite-0 hit
// lands (see WaitThenReactToSpriteZero in NMI).
static void ApplyHudSplit();

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

void CoinVramPush(const u16 address, const u8 value) {
    if (CoinVramLen + 3 > kCoinVramCap) return;        // full: drop (next frame retries)
    CoinVram[CoinVramLen++] = static_cast<u8>(address >> 8);
    CoinVram[CoinVramLen++] = static_cast<u8>(address & 0xFF);
    CoinVram[CoinVramLen++] = value;
}

// The sprite-0 split scrolls the playfield down one metatile (SetScroll(.,16))
// and level columns are written one metatile below the top (nametable row 2),
// so on screen level-data row 0 sits a HUD row below worldSpace.y 0. World-row 0
// is the HUD strip itself -- a real part of world space, just with no level data.
// kHudRows lives in levels.hpp now so the collision bitmap (producer) and this
// actor row projection (consumer) share one origin; reach it via level::kHudRows.
using demo::level::kHudRows;

level::Player player1;
#ifdef PLAYER2_SUPPORTED
level::Player player2;
#endif

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
static oam::oam_t SpriteY(u16 i);
static oam::oam_t SpriteX(u16 i);
#ifdef PLAYER2_SUPPORTED
static oam::oam_t SpriteY2(u16 i);
static oam::oam_t SpriteX2(u16 i);
#endif
static i16  ClampRow(u16 y);

RESET {
#ifdef TARGET_NES
    // init bank state -- window1Control/2/3 and chr0Control/1 are VRC1
    // registers, only defined off vrc1.cpp (NES-only, see vrc1.hpp).
    SwitchBank(window1Control, 0);
    SwitchBank(window2Control, 1);
    SwitchBank(window3Control, 2);
    SwitchCHRBank(chr0Control, 0);
    SwitchCHRBank(chr1Control, 1);
#endif

    if (!level::LoadLevel(0)) {
        reset();    // spin reset on NES, exit on SDL3
    }

    ppu::Flush(chrHUDWhitespace_tile, 0x11);

    oam::PopulateFromProvider(OAMBuffer, 0, oam::y, Clear, 64);

    // Sprite-0 hit anchor: placed at OAM (0,0) → renders at scanline 1 pixel 1
    // due to the OAM Y+1 offset.  The hit fires at a fixed PPU dot each frame;
    // NMI spin-waits for it directly (see WaitThenReactToSpriteZero below).
    // Requires a non-transparent BG pixel at nametable (0,0).
    OAMBuffer[0] = { 0, chrSprite0_tile, 0, 0 };

    // fill in with mario metatiles
    oam::PopulateFromBuffer(  OAMBuffer, 1, oam::tile, msMary,  kMarySprites);
    oam::PopulateFromProvider(OAMBuffer, 1, oam::y, SpriteY,  kMarySprites);
    oam::PopulateFromProvider(OAMBuffer, 1, oam::x, SpriteX,  kMarySprites);

#ifdef PLAYER2_SUPPORTED
    // player 2: horizontally flipped, OAM slots 5..8
    oam::PopulateFromBuffer(  OAMBuffer, 1 + kMarySprites, oam::tile, msMary2, kMarySprites);
    oam::PopulateFromProvider(OAMBuffer, 1 + kMarySprites, oam::y, SpriteY2, kMarySprites);
    oam::PopulateFromProvider(OAMBuffer, 1 + kMarySprites, oam::x, SpriteX2, kMarySprites);
#endif

    ppu::pal::WriteFromBuffer(ppu::BG_0,         SIZED_OBJ(BGColours));
    ppu::pal::WriteFromBuffer(ppu::SPRITE_0 + 1, SIZED_OBJ(maryColors));
    ppu::WriteFromBufferToNameTable(video::viewport_tx() - sizeof(msg_mary), 0, SIZED_OBJ(msg_mary), 0);

    constexpr u8 coinUI[] = {chrHUDCoin_tile, chrFont_tile + 0, chrFont_tile + 0};
    ppu::WriteFromBufferToNameTable(video::viewport_tx() - sizeof(coinUI), 1, SIZED_OBJ(coinUI), 0);

    level::edgeR = { level::TileData };
    level::dynEdgeR = { level::DynLengths, level::DynData, 0 }; // dyn forward edge, lockstep w/ edgeR
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
    level::edgeL = { level::TileData };
    level::dynEdgeL = { level::DynLengths, level::DynData, 0 }; // dyn backward edge, lockstep w/ edgeL

    ppu::SetScroll(0, 0);

    AudioInit();
    TrackPlay(0);

    // Seed the shared composite-metatile window over columns [0..kColMapWidth-1]
    // from the level start: a static Cursor and a dynamic Cursor both parked on
    // (col 0, row 0), composited per cell.  cameraX is 0 here, so the window is
    // already centred; ColMapTrack slides it as the camera scrolls.
    level::ColMapSeed(0, { level::TileData }, { level::DynLengths, level::DynData, 0 });

    player1.actor.size = { 16, 16 };
    player1.Reset();

#ifdef PLAYER2_SUPPORTED
    player2.actor.size = { 16, 16 };
    player2.Reset();
#endif
    oam::RefreshSprites(OAMBuffer);   /* seed the first frame's sprite snapshot */
    ppu::EnableRendering(ppu::ctrl::BG_ADDR, ppu::mask::BG_L | ppu::mask::SPRITE_L);

    apu::DisableFrameIRQ();
    apu::DisableDMCIRQ();
    EnableInterrupts();
    // ReSharper disable once CppDFAEndlessLoop
    while (!quit) {
        if (port1 & START) {
#ifndef  TARGET_NES
            quit = 1;
#endif
        }

        ppu::SetColorPriority(0x40);   // green band:        player1.Update
        player1.Update();
#ifdef PLAYER2_SUPPORTED
        ppu::SetColorPriority(0x60);   // red+green band:    player2.Update
        player2.Update();
#endif
        ppu::SetColorPriority(0x80);   // blue band:         ColMapTrack (slides override internally)

        level::ColMapTrack(cameraX >> 4);

        OAMBuffer[0] = { 7, chrSprite0_tile, 0, 0 };   // re-assert after player update

        ppu::SetColorPriority(0x20);   // red band:          AudioUpdate
        AudioUpdate();
        ppu::SetColorPriority(0);

        video::WaitForPresent();

#if TARGET_NES
        nmi_done = false;
        while (!nmi_done) {}
#endif
    }
}

interrupt nmiHandler() {
    lastPort1 = port1; lastPort2 = port2;
    PollControllers(&port1, &port2);
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

    // Plain, synchronous wait for the real sprite-0 hit (fires at a fixed PPU
    // dot every frame), then apply the HUD split. Blocks NMI well past
    // vblank into the active picture -- that's fine, the PPU keeps rendering
    // with whatever scroll is currently set regardless of what the CPU is
    // doing, and the main loop is already just polling nmi_done.
    WaitThenReactToSpriteZero(0, 16, ApplyHudSplit, &spriteZeroHandled);

    nmi_done = true;
}

static oam::oam_t Clear(const u16 _) {
    return 0xef;
}

// Provider shims: oam::PopulateFromProvider hands the callback only the sprite
// index, so bind the player Actor here and forward to the Actor-aware versions.
static oam::oam_t SpriteY(const u16 i)  { return AdjustSpriteY(&player1.actor, i); }
static oam::oam_t SpriteX(const u16 i)  { return AdjustSpriteX(&player1.actor, i); }
#ifdef PLAYER2_SUPPORTED
static oam::oam_t SpriteY2(const u16 i) { return AdjustSpriteY(&player2.actor, i); }
static oam::oam_t SpriteX2(const u16 i) { return AdjustSpriteX(&player2.actor, i); }
#endif

static i16 ClampRow(const u16 y) {
    if (y & 0x8000) return 0;
    const i16 r = static_cast<i16>(y >> 4) - kHudRows;
    if (r < 0) return 0;
    return r < level::levelHeight ? r : level::levelHeight - 1;
}

// Reaction passed to ::WaitThenReactToSpriteZero (called directly from NMI):
// the actual HUD split, applied once the beam is confirmed at (0, 16).
static void ApplyHudSplit() {
    #if defined(TARGET_NDS) || defined(TARGET_GBA)
        // --- cropped-panel vertical follow camera --------------------------------
        // The DS (256x192) and GBA (240x160) panels are shorter than the NES frame
        // (240px), so they cannot show the whole vertical slice the NES game renders.
        // While the player is low we bottom-anchor -- scroll Y bumped by the shortfall
        // (240 - viewport_py(): 48 on DS, 80 on GBA) so the ground sits on the panel
        // bottom. Once the player climbs to the middle of the viewport we pan up with
        // them, easing the bump from that maximum down to 0 (top-anchored) as they rise,
        // so they never clip off the top edge. The bump is just the player's height above
        // the viewport midpoint, clamped to [0, 240 - viewport_py()]. The math reads
        // viewport_py() so it adapts to either panel. Sprites are kept locked to this
        // varying scroll by the backend (build_sprites offsets every OBJ by the live band
        // scroll), so the whole vertical-follow policy lives here in one place. Every
        // full-height target renders the full 240 lines and needs no vertical camera
        // (the #else branch).
        const i16 mid    = static_cast<i16>(video::viewport_py() >> 1);
        const i16 anchor = static_cast<i16>(240 - video::viewport_py());
        const i16 raw    = static_cast<i16>(player1.actor.screen.y) - mid;
        const i16 bump   = raw < 0 ? 0 : (raw > anchor ? anchor : raw);
        ppu::SetScroll(cameraX, static_cast<u16>(16 + bump));
    #else
        ppu::SetScroll(cameraX, 16);
    #endif
}

NAKED_NMI { JUMP(nmiHandler); }