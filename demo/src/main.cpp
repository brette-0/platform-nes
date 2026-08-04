#include <platform-nes>
#include "header.hpp"
#include "main.hpp"

#include "graphics/colours.hpp"
#include "graphics/strings.hpp"
#include "graphics/graphics.hpp"
#include "graphics/metasprites.hpp"
#include "graphics/metatiles.hpp"

#include "level/levels.hpp"
#include "actor.hpp"
#include "level/collision_map.hpp"
#include "level/dynamic.hpp"
#include "level/player.hpp"

#include <platform-nes/apu.hpp>
#include <platform-nes/mappers/mmc3.hpp>

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

constexpr u8 kCoinVramCap = 48;         // 16 tile writes = 4 coins (2x2 each)
static u8 CoinVram[kCoinVramCap];
static u8 CoinVramLen;                  // bytes used

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

atomic tech::enum_flags<eLevelStreamCommands> levelStreamCommand;
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
    // init bank state -- window1Control/window2Control and chr0Control-
    // chr5Control are MMC3 registers, only defined off mmc3.cpp (NES-only,
    // see mmc3.hpp). mmc3.cpp's own ::_reset/SyncBankShadows already set
    // window1Control/window2Control to banks 0/1 at boot -- reasserted here
    // for the same "this is the demo's own explicit choice, not just
    // trusting boilerplate" reason the old VRC1 version did (there is no
    // window3Control/bank-2 equivalent to reassert: MMC3 in PRG mode 0 has
    // no register for $C000 at all, see mmc3.hpp's own file comment).
    //
    // CHR: VRC1's chr0Control/chr1Control were two 4 KiB windows (pattern
    // tables 0/1). MMC3 carves the same 8 KiB CHR space into six finer
    // windows instead -- R0/R1 (2 KiB each) cover $0000-$0FFF, R2-R5
    // (1 KiB each) cover $1000-$1FFF -- so VRC1's chr0Control=0 (physical
    // bytes [0,4096)) becomes R0=0/R1=1 (bytes [0,2048)+[2048,4096)), and
    // chr1Control=1 (bytes [4096,8192)) becomes R2=4/R3=5/R4=6/R5=7 (four
    // consecutive 1 KiB banks covering the same [4096,8192) range) -- same
    // physical CHR-ROM content, just addressed at MMC3's own granularity.
    mmc3::SwitchBank(mmc3::window1Control, 0);
    mmc3::SwitchBank(mmc3::window2Control, 1);
    mmc3::SwitchCHRBank(mmc3::chr0Control, 0);
    mmc3::SwitchCHRBank(mmc3::chr1Control, 1);
    mmc3::SwitchCHRBank(mmc3::chr2Control, 4);
    mmc3::SwitchCHRBank(mmc3::chr3Control, 5);
    mmc3::SwitchCHRBank(mmc3::chr4Control, 6);
    mmc3::SwitchCHRBank(mmc3::chr5Control, 7);

    // Mirroring ($A000) is the one MMC3 register nothing above reasserts, and
    // mmc3.cpp's own ::_reset deliberately leaves it alone (real hardware
    // powers it up undefined -- see that function's comment). VRC1 had no
    // mirroring register at all, so this engine's column-streaming address
    // math (nt_h selects PPU A10 as the horizontal-neighbor bit -- see
    // player.cpp's PushCoinVram) was implicitly relying on VERTICAL mirroring
    // (side-by-side nametables) the whole time without ever having to say so.
    // Under MMC3 that assumption is no longer free: with the register
    // unwritten, mirroring is whatever the mapper happens to power up with on
    // a given board/emulator, and if that lands on HORIZONTAL instead, PPU
    // A10 stops being the live nametable-select bit -- every other 32-tile
    // world-screen then aliases onto the SAME physical attribute byte as its
    // neighbour, so a column build for one screen silently corrupts an
    // attribute nibble that's still on screen from the other. Bit 0: 0 =
    // vertical, 1 = horizontal (opposite polarity from the iNES header's own
    // mirroring bit -- a well-known MMC3 gotcha, not a typo).
    mmc3::mirroring = 0;
#endif

    if (!level::LoadLevel(0)) {
        irq::reset();    // spin reset on NES, exit on SDL3
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

    edgeRAbs     = (1 + viewport_mx()) * level::levelHeight;
    level::edgeL = { level::TileData };
    level::dynEdgeL = { level::DynLengths, level::DynData, 0 }; // dyn backward edge, lockstep w/ edgeL

    ppu::SetScroll(0, 0);

    audio::AudioInit();
    audio::TrackPlay(0);

    // Seed the shared composite-metatile window over columns [0..kColMapWidth-1]
    // from the level start: a static Cursor and a dynamic Cursor both parked on
    // (col 0, row 0), composited per cell.  cameraX is 0 here, so the window is
    // already centred; ColMapTrack slides it as the camera scrolls.
    level::ColMapSeed(0, { level::TileData }, { level::DynLengths, level::DynData, 0 });

    player1.Reset();

#ifdef PLAYER2_SUPPORTED
    player2.Reset();
#endif
    oam::RefreshSprites(OAMBuffer);   /* seed the first frame's sprite snapshot */
    ppu::EnableRendering(ppu::ctrl::BG_ADDR, ppu::mask::BG_L | ppu::mask::SPRITE_L);

    apu::DisableFrameIRQ();
    apu::DisableDMCIRQ();
    irq::EnableInterrupts();
    // ReSharper disable once CppDFAEndlessLoop
    while (!quit) {
        if (port1 & input::START) {
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
        audio::AudioUpdate();
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
    input::PollControllers(&port1, &port2);
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
    video::WaitThenReactToSpriteZero(0, 16, ApplyHudSplit, &spriteZeroHandled);

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