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
    static ui::choice::SingleChoice<kMenuOptions>* pMenu = nullptr;
    static atomic u8 framePressed = 0;
    static u8 prevInputs = 0;

    // UI interaction write buffer -- see nmi_handler. 6 bytes: 2 ops
    // (SingleChoice's worst case today, one erase-old + one draw-new write
    // per Next()/Previous()), 3 bytes each (addr-hi, addr-lo, val).
    static u8 writeBuf[6];

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
        ui::choice::SingleChoice<kMenuOptions> menu(
            SIZED_OBJ(msg_menu),
            {menuCol, static_cast<u16>(kBottomRightNT + 1)},
            {kMenuBoxWidth, kMenuOptions},
            chrHUDWhitespace_tile, 0,
            ui::text::Alignment::Left,
            chrHUDWhitespace_tile, chrArrow_tile
        );
        pMenu = &menu;
        prevInputs = 0;
        framePressed = 0;

        ppu::SetScroll({0, 0xff});
        ppu::EnableRendering(ppu::ctrl::SPRITE_ADDR | ppu::ctrl::SPRITE_SIZE | ppu::ctrl::GEN_NMI, ppu::mask::BG_L | ppu::mask::SPRITE_L);

        irq::EnableInterrupts();

        while (true) {
            const u8 pressed = framePressed; // this frame's edge-detected input, published by nmi_handler

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

        if (pMenu) {
            u8* cursor = writeBuf;
            pMenu->Pass(pressed, cursor);   // advances cursor past whatever it wrote
            // Drain: the actual pokes, now that we're somewhere safe to do
            // them -- no arithmetic here, just replaying precomputed
            // (addr-hi, addr-lo, val) triples.
            for (u8* p = writeBuf; p < cursor; p += 3) {
                const int addr = (static_cast<int>(p[0]) << 8) | p[1];
                ppu::WriteSingleToNameTable(addr, p[2]);
            }
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
        // No explicit attribute writes here: the title/menu region only
        // ever needed a constant 0xff, which main()'s
        // ppu::Flush(chrHUDWhitespace_tile, 0xff) already stamped across
        // every nametable's attribute bytes (all 4 pages, under this
        // board's four-screen wiring) before this runs -- writing the same
        // constant again over the title/menu region was always redundant.

        // Title card lives in the bottom-right nametable now, not stacked
        // above the menu items in the top-right one -- same box size and
        // the same 1-tile padding it always had, just anchored to that
        // page's own top row instead of splitRow.
        ui::text::Draw(
            SIZED_OBJ(msg_title),
            {kMenuNT + 1, static_cast<u16>(kBottomRightNT + 1)},
            {static_cast<u8>((viewport_mx() >> 1) - 1), 3}, chrHUDWhitespace_tile,
            ui::text::Alignment::Left
        );

        // Menu items + selection cursor: drawn by the ui::choice::SingleChoice
        // constructed in main(), not here -- see its own comment there.
    }

    static oam::oam_t Clear(const u16) {
        return 0xf0;
    }

    static NI void DrawLevelPreview() {
        using namespace level;

        const bool loaded = mmc3::CallInBlock<level_code_tag>([] { return LoadLevel(0); });
        if (!loaded) return;

        // Full playfield, uncropped: HUD occupies tile rows 0-1 (kHudRows
        // metatile row), the level fills all 14 metatile rows (28 tile
        // rows) below it -- 2 + 28 = 30, the whole nametable height. What's
        // visible above SplitRow() vs covered by the menu split is a
        // display-time concern (ApplySplit's scroll switch); NT0 itself is
        // preloaded in full regardless of where the split lands.
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

        // Two metatile columns at a time (an attribute cell's width): buffer
        // both composited columns, then emit their tiles and attribute
        // bytes together, since one attribute byte packs all four quadrants
        // of a 2x2-metatile block and needs all four ids to compute.
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
            // Attribute cells are a hardware-fixed 4-tile-row grid starting
            // at absolute nametable row 0, but tyBase (2 tile rows of HUD)
            // shifts every metatile row's tile_row to an odd multiple of 2
            // -- so a metatile row pair (br, br+1) does NOT land in one
            // cell the way pairing by loop index assumes; the real pairing
            // is offset by one metatile row (see level::GetNextWrite, which
            // handles this same kHudRows shift for the live level). Classify
            // each metatile row independently by its own tile_row instead
            // of assuming an aligned pair, and accumulate into a per-cell
            // buffer since two DIFFERENT loop iterations can (correctly)
            // contribute to the same byte.
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

        // Stand on top of the ground row (levelHeight-2/-1, TileData's
        // bottom two rows), feet flush with its top edge. groundNtRow is
        // where that edge lands in NT0 (nametable/pixel space, tyBase
        // offset included); PreviewScrollY() converts it to screen space,
        // same as nmi_handler's own scroll write.
        const u16 groundNtRow = tyBase * 8 + (levelHeight - 2) * 16;
        const i16 rawFeetY    = static_cast<i16>(groundNtRow) - 16 - static_cast<i16>(PreviewScrollY());
        const auto feetY      = static_cast<oam::oam_t>(rawFeetY < 0 ? 0 : rawFeetY);
        OAMBuffer[0].y = feetY; OAMBuffer[1].y = feetY;
        OAMBuffer[0].x = 32;    OAMBuffer[1].x = 40;
    }
}
