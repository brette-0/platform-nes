#include "level.hpp"

#include <platform-nes/apu.hpp>
#include <platform-nes/mappers/mmc3.hpp>

#include "../banks.hpp"

#include "../graphics/colours.hpp"
#include "../graphics/strings.hpp"
#include "../graphics/graphics.hpp"
#include "../graphics/metasprites.hpp"
#include "../graphics/metatiles.hpp"

#include "level/levels.hpp"
#include "level/collision_map.hpp"
#include "level/dynamic.hpp"

using namespace demo;
using enum level::eLevelStreamCommands;

namespace level {
    bool paused;

    static volatile bool nmi_done;

    u8 port1;
    u8 port2;

    u8 lastPort1;
    u8 lastPort2;

    constexpr u8 kHudSplitRow   = 16;
    constexpr u8 kHudSplitMMC3  = 16 - 2;   // NOTE:: if we are stuffed for cpu time, lower this and hand-write ASM
    constexpr u8 kHUDSplitDelay = 62;

    // Forward-declared so irq_handler can call it directly once the MMC3
    // scanline IRQ fires below the HUD (see kHudSplitLatch / irq_handler).
    static FIXED void ApplyHudSplit();

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

    // kHudRows lives in levels.hpp so the collision bitmap (producer) and the
    // actor row projection (ClampRow, below) share one origin.

    Player player1;
#ifdef PLAYER2_SUPPORTED
    Player player2;
#endif

    i8 lastDeltaScroll;

    // World-pixel boundary of the most recently streamed column (always a multiple
    // of 16).  Doubles as the scroll hysteresis marker: a new column is only built
    // once the camera has travelled a full 16px column past it, so jitter or a
    // direction reversal near a boundary can't rapidly toggle BuildNext/BuildPrev
    // and drift the edge cursors.  Read by the NMI to place the nametable write.
    atomic u16 lastXWorldSpace;

    // Right edge's absolute metatile offset from the level start. Tracked so the
    // left edge can be parked exactly kEdgeGap metatiles behind it without an O(n)
    // re-walk: edgeL's target is derived from this counter, not from a one-shot seek.
    static u16 edgeRAbs;

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
    static i16 ClampRow(u16 y);

    // NOT pinned to the fixed bank, deliberately. This was ::FIXED while it
    // opened with SwitchBank(window1Control/window2Control, ...) -- code that
    // switches a PRG window must not be able to unmap itself mid-execution
    // (interrupts.hpp's RESET macro comment has the same hazard). Boot now
    // establishes R6/R7 (mmc3.cpp's ::_reset, via mmc3-helper.ld's
    // __mmc3_boot_bank_window1/2), so those two calls are gone and only the
    // SwitchCHRBank calls below remain -- R0-R5 move PPU pattern-table
    // windows and cannot touch PRG mapping, so there is nothing left to
    // protect against.
    //
    // Untagged, so the mangled-name rule in demo/link.ld sweeps it into the
    // level domain along with the rest of this namespace. Reached by a plain
    // call from main.cpp's dispatcher, which maps both of level's banks first
    // (EnterLevelBanks) -- not by a farcall, which can only switch one window
    // and would leave half the domain missing.
    // Everything level entry needs done exactly once -- never again for the
    // rest of the level's lifetime -- lives here instead of inline in ::main,
    // so it can be farcalled into the COLD bank (banks.hpp's ::cold_tag)
    // instead of permanently occupying the level domain's own tight 16 KiB.
    //
    // Safe to call from inside level's own domain (window1 = bank 0) even
    // though this runs in a DIFFERENT bank (bank 4, also window 1): the
    // farcall trampoline is ::FIXED, so it survives window 1 changing under
    // it, and window 2 (level's bank 1, holding ColMapStamp et al.) is never
    // touched by this call -- only window 1 moves. Ordinary in-domain calls
    // this makes into level's own bank 1 (ColMapSeed -> ColMapStamp) resolve
    // exactly as they did when inlined into ::main, because window 2 never
    // stops showing that bank while this runs.
    //
    // noinline: this is the ONLY call site, so without it LTO inlines the
    // whole body straight into CallInBlock's ::FIXED trampoline instead of
    // leaving it in ::COLD's own section -- defeating the point (confirmed
    // empirically: prg_rom_cold measured 0 bytes used and prg_rom_fixed
    // ballooned by ~5.2 KiB before this attribute was added).
    static COLD __attribute__((noinline)) void EnterLevelSetup() {
        mmc3::SwitchCHRBank(mmc3::chr0Control, 4);
        mmc3::SwitchCHRBank(mmc3::chr1Control, 5);
        mmc3::SwitchCHRBank(mmc3::chr2Control, 0);
        mmc3::SwitchCHRBank(mmc3::chr3Control, 1);
        mmc3::SwitchCHRBank(mmc3::chr4Control, 6);
        mmc3::SwitchCHRBank(mmc3::chr5Control, 7);
        mmc3::SetMirroring(false);

        if (!LoadLevel(0)) {
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

        edgeR = { TileData };
        dynEdgeR = { DynLengths, DynData, 0 }; // dyn forward edge, lockstep w/ edgeR
        for (auto i = 0; i < 2 + video::viewport_tx(); i += 2) {
            ppu::WriteFromProviderToNameTable(
                i, 2,
                GetNextWrite, 28, 1
            );

            ppu::WriteFromProviderToNameTable(
                i + 1, 2,
                GetCurrentNext, 28, 1
            );

            ppu::WriteFromBufferToAttributeTable(i & ~3, 2, AttributeBuffer, 8, 1);
        }

        edgeRAbs = (1 + viewport_mx()) * levelHeight;
        edgeL = { TileData };
        dynEdgeL = { DynLengths, DynData, 0 }; // dyn backward edge, lockstep w/ edgeL

        ppu::SetScroll(0, 0);

        // Long-called into the audio banks: the module and engine share one
        // (banks.hpp's audio_tag), the songs and SFX are in another. Both are
        // mapped for the duration; inside, the module calls the engine
        // directly and the engine reads its data directly.
        InAudioBanks([] {
            audio::Init(REGION);
            audio::TrackPlay(0);
        });

        ColMapSeed(0, { TileData }, { DynLengths, DynData, 0 });

        player1.Reset();

#ifdef PLAYER2_SUPPORTED
        player2.Reset();
#endif
        oam::RefreshSprites(OAMBuffer);   /* seed the first frame's sprite snapshot */
        ppu::EnableRendering(ppu::ctrl::SPRITE_SIZE | ppu::ctrl::SPRITE_ADDR, ppu::mask::BG_L | ppu::mask::SPRITE_L);

        apu::DisableFrameIRQ();
        apu::DisableDMCIRQ();
    }

    void main() {
        InColdBank(EnterLevelSetup);
        irq::EnableInterrupts();
        // ReSharper disable once CppDFAEndlessLoop
        while (!quit) {
            input::PollControllers(&port1, &port2);
            if (port1 & input::START && (port1 ^ lastPort1) & input::START) {
                paused = !paused;
            }

            ppu::SetColorPriority(0x40);   // green band:        player1.Update
            player1.Update();
#ifdef PLAYER2_SUPPORTED
            ppu::SetColorPriority(0x60);   // red+green band:    player2.Update
            player2.Update();
#endif
            ppu::SetColorPriority(0x80);   // blue band:         ColMapTrack (slides override internally)

            ColMapTrack(cameraX >> 4);

            ppu::SetColorPriority(0x20);   // red band:          Update
            InAudioBanks([] { audio::Update(); });
            ppu::SetColorPriority(0);

            video::WaitForPresent();

#if TARGET_NES
            nmi_done = false;
            while (!nmi_done) {}
#endif
        }
    }

    // ::FIXED, not banked. An interrupt fires with whatever banks happen to
    // be mapped -- including the audio pair mid-farcall, when BOTH halves of
    // level's domain are swapped out -- so anything a vector can reach has to
    // live somewhere no register can displace. That is the fixed bank.
    FIXED void nmi_handler() {
        lastPort1 = port1; lastPort2 = port2;
        oam::RefreshSprites(OAMBuffer);

        using enum eLevelStreamCommands;
        if (levelStreamCommand & eLevelStreamCommands::STREAM_LEVEL_DONE) SHADOW(ppu::PPUMASK) {
            ppu::PPUMASK = 0;
            if (levelStreamCommand & eLevelStreamCommands::STREAM_LEVEL_RIGHT) {
                ppu::WriteFromBufferToNameTable((lastXWorldSpace >> 3) + video::viewport_tx() + 0, 2, TileBuffer, 28, 1);
                ppu::WriteFromBufferToNameTable((lastXWorldSpace >> 3) + video::viewport_tx() + 1, 2, TileBuffer + 28, 28, 1);
                if (!(levelStreamCommand & eLevelStreamCommands::STREAM_LEVEL_SWAP))
                    ppu::WriteFromBufferToAttributeTable((lastXWorldSpace >> 3) + video::viewport_tx() & ~3, 2, AttributeBuffer, 8, 1);
            } else {
                ppu::WriteFromBufferToNameTable((lastXWorldSpace >> 3) - 1, 2, TileBuffer, 28, 1);
                ppu::WriteFromBufferToNameTable((lastXWorldSpace >> 3) - 2, 2, TileBuffer + 28, 28, 1);
                if (!(levelStreamCommand & eLevelStreamCommands::STREAM_LEVEL_SWAP))
                    ppu::WriteFromBufferToAttributeTable((lastXWorldSpace >> 3) - 2 & ~3, 2, AttributeBuffer, 8, 1);
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
        mmc3::ScheduleScanlineIRQ(kHudSplitMMC3, {0, kHudSplitRow});

        nmi_done = true;
    }

    // ::FIXED for the same reason as nmi_handler, and it calls ApplyHudSplit
    // directly, which is therefore ::FIXED too.
    FIXED void irq_handler() {
        mmc3::AcknowledgeScanlineIRQ();
        tech::SpinWait(kHUDSplitDelay);
        ApplyHudSplit();
    }

    static oam::oam_t Clear(const u16) {
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
        return r < levelHeight ? r : levelHeight - 1;
    }

    static FIXED void ApplyHudSplit() {
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
}
