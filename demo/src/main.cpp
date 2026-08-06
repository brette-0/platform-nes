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

// Forward-declared so irqHandler can call it directly once the MMC3
// scanline IRQ fires below the HUD (see kHudSplitLatch / irqHandler).
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
#ifndef TARGET_NES
    // Real NES hardware does CHR bank switching in silicon -- there's no
    // software tile-address translation to bind there. ppu::BindTileTranslator
    // and mmc3::GetTileLMA only exist off-NES (see video.hpp/mmc3.hpp).
    ppu::BindTileTranslator(mmc3::GetTileLMA);
#endif
    mmc3::SwitchBank(mmc3::window1Control, 0);
    mmc3::SwitchBank(mmc3::window2Control, 1);
    mmc3::SwitchCHRBank(mmc3::chr0Control, 4);
    mmc3::SwitchCHRBank(mmc3::chr1Control, 5);
    mmc3::SwitchCHRBank(mmc3::chr2Control, 0);
    mmc3::SwitchCHRBank(mmc3::chr3Control, 1);
    mmc3::SwitchCHRBank(mmc3::chr4Control, 6);
    mmc3::SwitchCHRBank(mmc3::chr5Control, 7);
    mmc3::SetMirroring(false);

    if (!level::LoadLevel(0)) {
        irq::reset();    // spin reset on NES, exit on SDL3
    }

    ppu::Flush(chrHUDWhitespace_tile, 0x11);

    oam::PopulateFromProvider(OAMBuffer, 0, oam::y, Clear, 64);

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

    level::ColMapSeed(0, { level::TileData }, { level::DynLengths, level::DynData, 0 });

    player1.Reset();

#ifdef PLAYER2_SUPPORTED
    player2.Reset();
#endif
    oam::RefreshSprites(OAMBuffer);   /* seed the first frame's sprite snapshot */
    ppu::EnableRendering(ppu::ctrl::SPRITE_SIZE | ppu::ctrl::SPRITE_ADDR, ppu::mask::BG_L | ppu::mask::SPRITE_L);

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

constexpr u8 kHudSplitRow   = 16;
constexpr u8 kHudSplitMMC3  = 16 - 1;

interrupt nmiHandler() {
    lastPort1 = port1; lastPort2 = port2;
    input::PollControllers(&port1, &port2);
    oam::RefreshSprites(OAMBuffer);

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
    mmc3::ScheduleScanlineIRQ(kHudSplitMMC3);

    nmi_done = true;
}

interrupt irqHandler() {
    mmc3::AcknowledgeScanlineIRQ();
    tech::SpinWait(0);
    ApplyHudSplit();
}

static oam::oam_t Clear(const u16 _) {
    return 0xf0;
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

// Called directly from irqHandler once the MMC3 scanline IRQ confirms the
// beam is in HBlank just above kHudSplitRow: the actual HUD split.
static void ApplyHudSplit() {
    #if defined(TARGET_NDS) || defined(TARGET_GBA)
        const i16 mid    = static_cast<i16>(video::viewport_py() >> 1);
        const i16 anchor = static_cast<i16>(240 - video::viewport_py());
        const i16 raw    = static_cast<i16>(player1.actor.screen.y) - mid;
        const i16 bump   = raw < 0 ? 0 : (raw > anchor ? anchor : raw);
        ppu::SetScroll(cameraX, static_cast<u16>(16 + bump));
    #else
        ppu::SetScroll(cameraX, kHudSplitRow);
    #endif
}

NAKED_NMI { JUMP(nmiHandler); }
NAKED_IRQ { JUMP(irqHandler); }