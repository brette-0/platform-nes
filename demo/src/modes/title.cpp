#include "title.hpp"
#include "../main.hpp"
#include "../banks.hpp"
#include "../graphics/colours.hpp"
#include "../graphics/strings.hpp"
#include "../graphics/graphics.hpp"
#include "../graphics/metasprites.hpp"
#include "level/levels.hpp"
#include "platform-nes/mappers/mmc3.hpp"
#include "platform-nes/extras/ui/singlechoice.hpp"

namespace title {
    constexpr u8 kMenuOptions   = static_cast<u8>(End + 1);
    // Longest label ("NEW GAME"/"CONTINUE", 8 chars) -- both the menu's box
    // width and, mirrored below, how far in from the screen edge it's placed.
    constexpr u8 kMenuBoxWidth  = 8;

    // Play-mode choice -- see msg_playMode. Not wired into the title screen
    // yet, just prepared: [[wire up play mode screen]] follow-up.
#if defined(TARGET_NES)
    constexpr u8 kPlayModeOptions  = 2;
    constexpr u8 kPlayModeBoxWidth = 12;   // "MULTIPLAYER"
#else
    constexpr u8 kPlayModeOptions  = 3;
    constexpr u8 kPlayModeBoxWidth = 17;   // "LOCAL MULTIPLAYER"
#endif
    static ui::choice::SingleChoice* pMenu = nullptr;

    // Play-mode picker: prepared (word-wrapped, optionAddr computed) at
    // startup but not drawn until New Game/Continue is picked -- see
    // nmi_handler_drawPlayMode.
    static ui::choice::SingleChoice* pPlayMode = nullptr;
    static buffer<u8*>* pPlayModeChunks = nullptr;
    static vec2<u16> playModePos;
    // Row-0 nametable address for playModePos, precomputed in main()'s loop
    // (ordinary context, a division there costs nothing worth avoiding) and
    // read by nmi_handler_drawPlayMode via SingleChoice::Draw's address
    // overload, so the ISR itself never pays CartesianToAddress's divide.
    static u16 playModeAddr;

    // UI interaction write buffer, and its encoding/draining -- entirely
    // this file's own decision, not SingleChoice's; see TitleSelect/
    // TitleUnselect. 6 bytes: 2 ops (one erase-old + one draw-new write per
    // Next()/Previous(), the only two VisualFn calls Pass() makes per call),
    // 3 bytes each (addr-hi, addr-lo, val).
    //
    // Built in main()'s loop, NOT in nmi_handler: Pass() (and whatever
    // TitleSelect/TitleUnselect cost) has zero business running inside the
    // vblank-critical ISR when the whole point of buffering is to let that
    // work happen somewhere it isn't time-boxed. nmi_handler's only job on
    // this buffer is the cheap, fixed-cost part: drain whatever's already
    // sitting in it.
    static u8 writeBuf[6];
    // Bytes currently pending in writeBuf, published by main()'s loop after
    // Pass() finishes building them and consumed (then reset to 0) by
    // nmi_handler -- single-byte atomic write/read, so an NMI landing
    // mid-build only ever sees either the previous complete count or 0,
    // never a torn buffer.
    static atomic u8 writeBufLen = 0;

    // Replays ops queued by WriteMenuOp between start and end, wherever
    // it's actually safe to poke the PPU.
    static void DrainWriteBuf(const u8* start, const u8* end) {
        for (const u8* p = start; p < end; p += 3) {
            const int addr = (static_cast<int>(p[0]) << 8) | p[1];
            ppu::WriteSingleToNameTable(addr, p[2]);
        }
    }

    static oam::oam_t Clear(u16 _);
    static NI void DrawLevelPreview();
    static void nmi_handler_drawPlayMode();

    constexpr u16 kMenuNT = 32;
    constexpr u16 kBottomRightNT = 30;

    // On-screen row where the IRQ crosses from the level (nametable 0) into
    // the menu (nametable 1) -- see nmi_handler/ApplySplit.
    static u8 SplitRow() {
        return (((viewport_my() + 1) >> 1) - 2) << 2;
    }

    static u16 PreviewScrollY() {
        return video::viewport_py() - (static_cast<u16>(SplitRow()) << 3);
    }

    // Mirrors level.cpp's kHUDSplitDelay -- same PAL-vs-NTSC IRQ-to-write
    // latency. Safe at 0 on NTSC because ApplySplit is a plain $2005 write,
    // not $2006 -- see its own comment.
    constexpr u8 kSplitDelay = REGION ? 90 : 0;
    static void ApplySplit();


    TITLE NI void main() {
        oam::PopulateFromProvider(OAMBuffer, 0, oam::y, Clear, 64);
        pIRQ = irq_handler;
        pNMI = nmi_handler;

        mmc3::SwitchCHRBank(mmc3::chr0Control, 4);
        mmc3::SwitchCHRBank(mmc3::chr1Control, 5);
        mmc3::SwitchCHRBank(mmc3::chr2Control, 0);
        mmc3::SwitchCHRBank(mmc3::chr3Control, 1);
        mmc3::SwitchCHRBank(mmc3::chr4Control, 6);
        mmc3::SwitchCHRBank(mmc3::chr5Control, 7);
        ppu::Flush(chrHUDWhitespace_tile, 0xff);
        ppu::pal::WriteFromBuffer(13, titleScreenColours, 3);

        DrawLevelPreview();
        InitTitleScreen();

        // Menu items -- all start at the same column, with a 1-tile gap from
        // the right edge for the longest entry (NEW GAME/CONTINUE). Anchored
        // to the bottom-right nametable's own top row, same as the title
        // card and for the same reason.
        const u16 menuCol = kMenuNT + (viewport_mx() << 1) - 1 - kMenuBoxWidth;
        u8* initCursor = writeBuf;
        ui::choice::SingleChoice menu(TitleUnselect, TitleSelect, kMenuOptions);
        const auto menuChunks = menu.Make(
            SIZED_OBJ(msg_menu),
            {menuCol, static_cast<u16>(kBottomRightNT + 1)},
            {kMenuBoxWidth, kMenuOptions},
            chrHUDWhitespace_tile, 0
        );
        menu.Draw(
            menuChunks,
            {menuCol, static_cast<u16>(kBottomRightNT + 1)},
            kMenuOptions,
            initCursor
        );
        delete[] menuChunks;
        // Initial selection indicator: SingleChoice only queued it into
        // writeBuf (see VisualFn) -- draining it here is safe because
        // nothing's rendering yet (NMI isn't enabled until below).
        DrainWriteBuf(writeBuf, initCursor);

        // Play-mode choice: prepared (word-wrapped, optionAddr computed)
        // but not drawn -- see kPlayModeOptions/msg_playMode's own comments.
        // Same row as the menu (drawn straight over that text later, see
        // nmi_handler_drawPlayMode) but its own right-anchored column --
        // its box is wider than the menu's ("LOCAL MULTIPLAYER" vs. "NEW
        // GAME"), so reusing menuCol verbatim would run the box off the
        // right edge of the viewport instead of fitting the text.
        const u16 playModeCol = kMenuNT + (viewport_mx() << 1) - 1 - kPlayModeBoxWidth;
        ui::choice::SingleChoice playMode(TitleUnselect, TitleSelect, kPlayModeOptions);
        playModePos = {playModeCol, static_cast<u16>(kBottomRightNT + 1)};
        pPlayModeChunks = playMode.Make(
            SIZED_OBJ(msg_playMode),
            playModePos,
            {kPlayModeBoxWidth, kPlayModeOptions},
            chrHUDWhitespace_tile, 0
        );
        pPlayMode = &playMode;
        pMenu = &menu;

        ppu::SetScroll({0, 0xff});
        ppu::EnableRendering(ppu::ctrl::SPRITE_ADDR | ppu::ctrl::SPRITE_SIZE | ppu::ctrl::GEN_NMI, ppu::mask::BG_L | ppu::mask::SPRITE_L);

        irq::EnableInterrupts();

        // Edge-detect state for the polling below -- local to main(), not
        // shared with the ISR: polling doesn't touch the PPU, so it has no
        // reason to run inside nmi_handler or to be published/consumed
        // across the ISR boundary the way writeBuf is.
        u8 prevInputs = 0;

        while (true) {
            u8 port1, port2;
            input::PollControllers(&port1, &port2);
            const u8 inputs  = port1 | port2;
            const u8 pressed = inputs & static_cast<u8>(~prevInputs); // strobe: only the frame a button goes down
            prevInputs = inputs;

            // Build this frame's pending selection-indicator writes here,
            // outside the ISR -- see writeBuf/writeBufLen's own comments.
            // Only touches writeBuf/writeBufLen if pMenu actually queues
            // something (Pass() no-ops on anything but UP/DOWN).
            if (pMenu) {
                u8* cursor = writeBuf;
                pMenu->Pass(pressed, cursor);
                writeBufLen = static_cast<u8>(cursor - writeBuf);
            }

            if (pressed & input::A) {
                switch (menu.option) {
                    case NewGame:
                    case Continue:
                        // Show the player-count picker over the current menu
                        // text instead of proceeding straight to gameplay --
                        // actually starting the level once a mode is picked
                        // is a follow-up, not wired up yet. One-shot NMI
                        // swap, not a flag: see nmi_handler_drawPlayMode.
                        // CartesianToAddress paid here, in ordinary code, not
                        // inside the ISR -- see playModeAddr's own comment.
                        playModeAddr = ppu::CartesianToAddress(playModePos);
                        pNMI = nmi_handler_drawPlayMode;
                        break;

                    case Options:
                        break;

#if defined(TARGET_MACOS) || defined(TARGET_WINDOWS) || defined(TARGET_LINUX)
                    case Quit:
                        quit = true;
                        return;
#endif

                    default: ;
                }
            }

            video::WaitForPresent();
            if (quit) return;
        }
        // No more break out of the loop above: New Game/Continue only show
        // the player-count picker now (see the switch above), they don't
        // proceed to gameplay by themselves. Actually leaving the title
        // screen (ppu::PPUMASK = 0; gameMode = eGameModes::Level;) is a
        // follow-up, triggered once a play mode is picked.
    }

    void nmi_handler() {
        // OAM DMA first: it needs to start as early into vblank as possible
        // to finish before sprite evaluation, so nothing goes ahead of it.
        oam::RefreshSprites(OAMBuffer);

        // writeBuf's actual contents are built in main()'s loop, not here --
        // see writeBuf's own comment. This is the only part that has to run
        // in the ISR: replay whatever's pending, then clear the count so a
        // quiet frame (nothing newly queued) doesn't redrain stale bytes.
        if (writeBufLen) {
            DrainWriteBuf(writeBuf, writeBuf + writeBufLen);
            writeBufLen = 0;
        }

        ppu::SetScroll({0, PreviewScrollY()});

        // Arm the preview/menu split for this frame -- see ApplySplit.
        // Reload value is latency-corrected the same way level.cpp's
        // kHudSplitMMC3 is (the IRQ fires a few scanlines late relative to
        // the reload count); position.y is the real target row, used as-is
        // by the off-NES software rasterizer.
        const u16 splitPixelRow = static_cast<u16>(SplitRow()) << 3;
        constexpr u8 kSplitLatency = REGION ? 4 : 3;
        const u8 splitReload = splitPixelRow > kSplitLatency
            ? static_cast<u8>(splitPixelRow - kSplitLatency) : 0;
        mmc3::ScheduleScanlineIRQ(splitReload, {0, splitPixelRow});
    }

    // One-shot: installed as pNMI by main()'s loop when New Game/Continue is
    // picked (see the switch there), instead of a flag polled every frame
    // inside the steady-state handler above -- this way the draw actually
    // runs unconditionally, on the very next vblank, as ordinary mainline
    // code in its own handler, not a rarely-true branch buried in the
    // handler that runs every other frame.
    //
    // Runs nmi_handler() first -- OAM DMA, the HUD split IRQ, etc. still
    // need to happen every frame regardless -- then draws the player-count
    // picker straight over the existing menu text (no clear first: same
    // Draw() that never clears, just overwrites), and hands back to
    // nmi_handler for every frame after.
    static void nmi_handler_drawPlayMode() {
        u8 indicatorBuf[3];
        u8* cursor = indicatorBuf;
        pPlayMode->Draw(pPlayModeChunks, playModeAddr, kPlayModeOptions, cursor);
        DrainWriteBuf(indicatorBuf, cursor);
        ppu::SetScroll({0, PreviewScrollY()});
        delete[] pPlayModeChunks;
        pPlayModeChunks = nullptr;

        pNMI = nmi_handler;
    }

    void irq_handler() {
        mmc3::AcknowledgeScanlineIRQ();
        tech::SpinWait(kSplitDelay);
        ApplySplit();
    }

    static void ApplySplit() {
        ppu::SetScroll({static_cast<u16>(kMenuNT << 3), static_cast<u16>(SplitRow() << 3)});
    }

    void InitTitleScreen() {
         const auto titleText = ui::text::Make(
                SIZED_OBJ(msg_title),
                {static_cast<u8>((viewport_mx() >> 1) - 1), 3}, chrHUDWhitespace_tile
            );
        ui::text::Draw(
            titleText,
            {kMenuNT + 1, static_cast<u16>(kBottomRightNT + 1)},
            3
        );

        delete[] titleText;
    }

    static oam::oam_t Clear(const u16) {
        return 0xf0;
    }

    static NI void DrawLevelPreview() {
        using namespace level;

        if (const bool loaded = mmc3::CallInBlock<level_code_tag>([] { return LoadLevel(0); }); !loaded) return;

        // PopulateNameTableColumns always draws at row 2 (kHudRows*2) --
        // see its own comment -- so this mirrors that placement for the
        // feet-Y math below instead of hardcoding a second copy of it.
        constexpr u16 tyBase = kHudRows * 2;

        CallLevelGraphics([] {
            ppu::WriteFromBufferToNameTable({static_cast<u16>(video::viewport_tx() - sizeof(msg_mary)), 0}, SIZED_OBJ(msg_mary), 0);
        });
        constexpr u8 coinUI[] = {chrHUDCoin_tile, chrFont_tile + 0, chrFont_tile + 0};
        ppu::WriteFromBufferToNameTable({static_cast<u16>(video::viewport_tx() - sizeof(coinUI)), 1}, SIZED_OBJ(coinUI), 0);

        const u16 colsWide  = nColumns < viewport_mx() ? nColumns : viewport_mx();
        // Floored to even: an attribute cell is a 2x2-metatile block, so an
        // odd column count (the 3DS's 25 metatile-wide viewport) would leave
        // a trailing half-block. Costs at most one preview column there.
        const u16 blockCols = colsWide & ~static_cast<u16>(1);

        // Same streaming fill EnterLevelSetup uses to draw the real level
        // view (see its own comment) -- reuses the level bank's metatile/
        // attribute logic for the preview instead of a second, independent
        // copy of it living here. blockCols counts metatile columns (2
        // tiles wide each); PopulateNameTableColumns counts tile columns,
        // same units as EnterLevelSetup's own viewport_tx()-based call.
        const u16 previewTileCols = static_cast<u16>(blockCols * 2);
        mmc3::CallInBlock<level_code_tag>([previewTileCols] {
            PopulateNameTableColumns(previewTileCols);
        });

        CallLevelGraphics([] {
            oam::PopulateFromBuffer(OAMBuffer, 0, oam::tile,       msMary, kMarySprites);
            oam::PopulateFromBuffer(OAMBuffer, 0, oam::attributes, msMary, kMarySprites);
            ppu::pal::WriteFromBuffer(ppu::SPRITE_0 + 1, SIZED_OBJ(maryColors));
            ppu::pal::WriteFromBuffer(0, BGColours, 4);
            ppu::pal::WriteFromBuffer(5, BGColours + 5, 3);
        });

        const u16 groundNtRow = tyBase * 8 + (levelHeight - 2) * 16;
        const i16 rawFeetY    = static_cast<i16>(groundNtRow) - 16 - static_cast<i16>(PreviewScrollY());
        const auto feetY      = static_cast<oam::oam_t>(rawFeetY < 0 ? 0 : rawFeetY);
        OAMBuffer[0].y = feetY; OAMBuffer[1].y = feetY;
        OAMBuffer[0].x = 32;    OAMBuffer[1].x = 40;
    }

    auto TitleUnselect(const u16 addr, u8*& buf) -> void {
        *buf++ = static_cast<u8>(addr >> 8);
        *buf++ = static_cast<u8>(addr & 0xFF);
        *buf++ = chrHUDWhitespace_tile;
    }

    auto TitleSelect(const u16 addr, u8*& buf) -> void {
        *buf++ = static_cast<u8>(addr >> 8);
        *buf++ = static_cast<u8>(addr & 0xFF);
        *buf++ = chrArrow_tile;
    }
}
