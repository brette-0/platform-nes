#include "title.hpp"
#include "../main.hpp"
#include "../banks.hpp"
#include "../graphics/colours.hpp"
#include "../graphics/strings.hpp"
#include "../graphics/graphics.hpp"
#include "../graphics/metasprites.hpp"
#include "../graphics/metatiles.hpp"
#include "level/levels.hpp"
#include "level/dynamic.hpp"
#include "platform-nes/mappers/mmc3.hpp"
#include "platform-nes/extras/ui/singlechoice.hpp"

namespace title {
    constexpr u8 kMenuOptions   = static_cast<u8>(End + 1);
    // Longest label ("NEW GAME"/"CONTINUE", 8 chars) -- both the menu's box
    // width and, mirrored below, how far in from the screen edge it's placed.
    constexpr u8 kMenuBoxWidth  = 8;
    static ui::choice::SingleChoice* pMenu = nullptr;
    static atomic u8 framePressed = 0;
    static u8 prevInputs = 0;

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
        pMenu = &menu;
        prevInputs = 0;
        framePressed = 0;

        ppu::SetScroll({0, 0xff});
        ppu::EnableRendering(ppu::ctrl::SPRITE_ADDR | ppu::ctrl::SPRITE_SIZE | ppu::ctrl::GEN_NMI, ppu::mask::BG_L | ppu::mask::SPRITE_L);

        irq::EnableInterrupts();

        while (true) {
            const u8 pressed = framePressed; // this frame's edge-detected input, published by nmi_handler

            // Build this frame's pending selection-indicator writes here,
            // outside the ISR -- see writeBuf/writeBufLen's own comments.
            // Only touches writeBuf/writeBufLen if pMenu actually queues
            // something (Pass() no-ops on anything but UP/DOWN), so most
            // frames this is just the framePressed read above.
            if (pMenu) {
                u8* cursor = writeBuf;
                pMenu->Pass(pressed, cursor);
                writeBufLen = static_cast<u8>(cursor - writeBuf);
            }

            bool proceed = false;
            if (pressed & input::A) {
                switch (menu.option) {
                    case NewGame:
                        proceed = true;
                        break;

                    case Continue:
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
            if (proceed) break;
        }

        ppu::PPUMASK = 0;
        gameMode = eGameModes::Level;
    }

    void nmi_handler() {
        // OAM DMA first, same as before this change: it needs to start as
        // early into vblank as possible to finish before sprite evaluation,
        // so nothing goes ahead of it -- input polling included.
        oam::RefreshSprites(OAMBuffer);

        u8 port1, port2;
        input::PollControllers(&port1, &port2);
        const u8 inputs  = port1 | port2;
        const u8 pressed = inputs & static_cast<u8>(~prevInputs); // strobe: only the frame a button goes down
        prevInputs = inputs;
        framePressed = pressed;

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

        const bool hasDynamic = DynLengths != nullptr;
        DynamicCursor dyn{DynLengths, DynData, 0};

        u8 colBuf[2][levelHeight];
        for (u16 mc = 0; mc < blockCols; mc += 2) {
            for (u8 dc = 0; dc < 2; ++dc) {
                const u8* col = TileData + (mc + dc) * levelHeight;
                for (u8 r = 0; r < levelHeight; ++r) {
                    u8 m = col[r];
                    if (hasDynamic) {
                        if (const u8 d = dyn.Fetch()) m = d;
                        dyn.Move(1);
                    }
                    colBuf[dc][r] = m;
                }
            }

            const u16 tx = mc << 1;   // tile-x of this block's left edge
            for (u8 r = 0; r < levelHeight; ++r) {
                const u8  mL = colBuf[0][r], mR = colBuf[1][r];
                const u16 ty = static_cast<u16>(tyBase + 2 * r);
                ppu::WriteSingleToNameTable({tx,     ty},     Metatiles_UL[mL]);
                ppu::WriteSingleToNameTable({static_cast<u16>(tx + 1), ty},     Metatiles_BL[mL]);
                ppu::WriteSingleToNameTable({tx,     static_cast<u16>(ty + 1)}, Metatiles_UR[mL]);
                ppu::WriteSingleToNameTable({static_cast<u16>(tx + 1), static_cast<u16>(ty + 1)}, Metatiles_BR[mL]);
                ppu::WriteSingleToNameTable({static_cast<u16>(tx + 2), ty},     Metatiles_UL[mR]);
                ppu::WriteSingleToNameTable({static_cast<u16>(tx + 3), ty},     Metatiles_BL[mR]);
                ppu::WriteSingleToNameTable({static_cast<u16>(tx + 2), static_cast<u16>(ty + 1)}, Metatiles_UR[mR]);
                ppu::WriteSingleToNameTable({static_cast<u16>(tx + 3), static_cast<u16>(ty + 1)}, Metatiles_BR[mR]);
            }
            u8 attrBuf[8] = {};
            attrBuf[0] = 0x0F;   // HUD rows 0-1 share attr cell 0's top quadrant with level row 0; pin it to palette 3
            for (u8 r = 0; r < levelHeight; ++r) {
                const u8 tileRow  = static_cast<u8>(tyBase + 2 * r);
                const u8 attrIdx  = tileRow >> 2;
                const u8 isBottom = (tileRow >> 1) & 1;
                const u8 palL = Metatiles_ATTR[colBuf[0][r]] & MetatilePaletteMask;
                const u8 palR = Metatiles_ATTR[colBuf[1][r]] & MetatilePaletteMask;
                attrBuf[attrIdx] |= static_cast<u8>(palL << (isBottom ? 4 : 0));
                attrBuf[attrIdx] |= static_cast<u8>(palR << (isBottom ? 6 : 2));
            }
            for (u8 cell = 0; cell < 8; ++cell) {
                ppu::WriteSingleToAttributeTable({tx, static_cast<u16>(cell * 4)}, attrBuf[cell]);
            }
        }

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
