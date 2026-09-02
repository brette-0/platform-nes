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

namespace title {
    // ReSharper disable once CppUseAuto
    atomic u8 menuOption = NewGame;
    // ReSharper disable once CppUseAuto
    static atomic u8 lastMenuOption = NewGame;
    static oam::oam_t Clear(u16 _);
    // NI: sole call site is main() (::TITLE, shares the tight ::COLD bank) --
    // without it LTO inlines the whole body there instead of the roomier
    // default resident bank (prg_rom_switchable).
    static NI void DrawLevelPreview();

    constexpr u16 kMenuNT = 32;

    // On-screen row where the IRQ crosses from the level (nametable 0) into
    // the menu (nametable 1) -- see nmi_handler/ApplySplit.
    static u8 SplitRow() {
        return (((viewport_my() + 1) >> 1) - 2) << 2;
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

        ppu::SetScroll({0, 0xff});
        ppu::EnableRendering(ppu::ctrl::SPRITE_ADDR | ppu::ctrl::SPRITE_SIZE | ppu::ctrl::GEN_NMI, ppu::mask::BG_L | ppu::mask::SPRITE_L);

        irq::EnableInterrupts();

        u8 port1, port2, prevInputs = 0;

        while (true) {
            input::PollControllers(&port1, &port2);

            const auto inputs  = port1 | port2;
            const auto pressed = inputs & static_cast<u8>(~prevInputs); // strobe: only the frame a button goes down
            prevInputs = inputs;

            if (pressed & (input::UP | input::DOWN)) {
                menuOption -= (pressed & input::UP)   == input::UP;
                if (menuOption > End) menuOption = 0;
                menuOption += (pressed & input::DOWN) == input::DOWN;
                if (menuOption > End) menuOption = End;
            }

            bool proceed = false;
            if (pressed & input::A) {
                switch (menuOption) {
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
        const u16 menuCol  = kMenuNT + (viewport_mx() << 1) - 1 - sizeof(msg_continue);
        const u16 menuRow1 = SplitRow() + 1;

        // write chrEmpty_tile where arrow was
        ppu::WriteSingleToNameTable({static_cast<u16>(menuCol - 2), static_cast<u16>(menuRow1 + lastMenuOption)}, chrHUDWhitespace_tile);
        // write chrArrow_tile where arrow now is
        ppu::WriteSingleToNameTable({static_cast<u16>(menuCol - 2), static_cast<u16>(menuRow1 + menuOption)}, chrArrow_tile);
        lastMenuOption = menuOption;

        oam::RefreshSprites(OAMBuffer);
        ppu::SetScroll({0, 0});

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
        const u8 splitRow  = SplitRow();
        const u8 menuRows  = static_cast<u8>((viewport_my() << 1) - splitRow);
        const u8 attrCells = static_cast<u8>((video::viewport_tx() + 3) >> 2);
        for (u8 r = 0; r < menuRows; r += 4) {
            ppu::WriteFromProviderToAttributeTable(
                {kMenuNT, static_cast<u16>(splitRow + r)},
                MenuAttributesProvider, attrCells, 0
            );
        }

        ui::text::Draw(
            SIZED_OBJ(msg_title),
            {kMenuNT + 1, static_cast<u16>(splitRow + 1)},
            {static_cast<u8>((viewport_mx() >> 1) - 1), 3}, chrHUDWhitespace_tile,
            ui::text::Alignment::Left
        );

        // menu items -- all start at the same column, with a 1-tile gap from
        // the right edge for the longest entry (msg_newGame/msg_continue).
        const u16 menuCol = kMenuNT + (viewport_mx() << 1) - 1 - sizeof(msg_continue);

        // selection cursor -- starts on New Game, one tile of gap before the text.
        ppu::WriteSingleToNameTable({static_cast<u16>(menuCol - 2), static_cast<u16>(splitRow + 1)}, chrArrow_tile);

        ppu::WriteFromBufferToNameTable({menuCol, static_cast<u16>(splitRow + 1)}, SIZED_OBJ(msg_newGame), 0);
        ppu::WriteFromBufferToNameTable({menuCol, static_cast<u16>(splitRow + 2)}, SIZED_OBJ(msg_continue), 0);
        ppu::WriteFromBufferToNameTable({menuCol, static_cast<u16>(splitRow + 3)}, SIZED_OBJ(msg_options), 0);
#if defined(TARGET_MACOS) || defined(TARGET_WINDOWS) || defined(TARGET_LINUX)
        // consoles have no OS to quit back to -- PC targets only.
        ppu::WriteFromBufferToNameTable({menuCol, static_cast<u16>(splitRow + 4)}, SIZED_OBJ(msg_quit), 0);
#endif
    }

    u8 MenuAttributesProvider(const u8) {
        return 0xff;
    }

    static oam::oam_t Clear(const u16) {
        return 0xf0;
    }

    static NI void DrawLevelPreview() {
        using namespace level;

        const bool loaded = mmc3::CallInBlock<level_code_tag>([] { return LoadLevel(0); });
        if (!loaded) return;

        // Bottom-align the crop: skip whatever doesn't fit from the TOP of
        // the level. Rounded down to an even metatile-row count so the
        // tile writes below never need a partial 2x2 attribute cell.
        u8 visibleMetaRows = static_cast<u8>((SplitRow() >> 1) & ~1u);
        if (visibleMetaRows > levelHeight) visibleMetaRows = levelHeight;
        if (visibleMetaRows < 2) return;   // no room above the menu -- skip the preview
        const u8 skipRows = static_cast<u8>(levelHeight - visibleMetaRows);

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
                if (hasDynamic) dyn.Move(static_cast<i8>(skipRows));
                for (u8 r = 0; r < visibleMetaRows; ++r) {
                    u8 m = col[skipRows + r];
                    if (hasDynamic) {
                        if (const u8 d = dyn.Fetch()) m = d;
                        dyn.Move(1);
                    }
                    colBuf[dc][r] = m;
                }
            }

            const u16 tx = mc << 1;   // tile-x of this block's left edge
            for (u8 r = 0; r < visibleMetaRows; ++r) {
                const u8  mL = colBuf[0][r], mR = colBuf[1][r];
                const u16 ty = static_cast<u16>(2 * r);
                ppu::WriteSingleToNameTable({tx,     ty},     Metatiles_UL[mL]);
                ppu::WriteSingleToNameTable({static_cast<u16>(tx + 1), ty},     Metatiles_BL[mL]);
                ppu::WriteSingleToNameTable({tx,     static_cast<u16>(ty + 1)}, Metatiles_UR[mL]);
                ppu::WriteSingleToNameTable({static_cast<u16>(tx + 1), static_cast<u16>(ty + 1)}, Metatiles_BR[mL]);
                ppu::WriteSingleToNameTable({static_cast<u16>(tx + 2), ty},     Metatiles_UL[mR]);
                ppu::WriteSingleToNameTable({static_cast<u16>(tx + 3), ty},     Metatiles_BL[mR]);
                ppu::WriteSingleToNameTable({static_cast<u16>(tx + 2), static_cast<u16>(ty + 1)}, Metatiles_UR[mR]);
                ppu::WriteSingleToNameTable({static_cast<u16>(tx + 3), static_cast<u16>(ty + 1)}, Metatiles_BR[mR]);
            }
            for (u8 br = 0; br < visibleMetaRows; br += 2) {
                const u8 palTL = Metatiles_ATTR[colBuf[0][br]]     & MetatilePaletteMask;
                const u8 palTR = Metatiles_ATTR[colBuf[1][br]]     & MetatilePaletteMask;
                const u8 palBL = Metatiles_ATTR[colBuf[0][br + 1]] & MetatilePaletteMask;
                const u8 palBR = Metatiles_ATTR[colBuf[1][br + 1]] & MetatilePaletteMask;
                const u8 attrByte = static_cast<u8>(palTL | (palTR << 2) | (palBL << 4) | (palBR << 6));
                ppu::WriteSingleToAttributeTable({tx, static_cast<u16>(2 * br)}, attrByte);
            }
        }

        CallLevelGraphics([] {
            oam::PopulateFromBuffer(OAMBuffer, 0, oam::tile,       msMary, kMarySprites);
            oam::PopulateFromBuffer(OAMBuffer, 0, oam::attributes, msMary, kMarySprites);
            ppu::pal::WriteFromBuffer(ppu::SPRITE_0 + 1, SIZED_OBJ(maryColors));
            ppu::pal::WriteFromBuffer(0, BGColours, 4);
            ppu::pal::WriteFromBuffer(5, BGColours + 5, 3);
        });

        // Stand on top of the ground row (visibleMetaRows-2/-1, the terrain
        // rows carried over from TileData's bottom two rows), feet flush
        // with its top edge.
        const u8  groundTileRow = static_cast<u8>(2 * (visibleMetaRows - 2));
        const i16 rawFeetY      = static_cast<i16>(groundTileRow) * 8 - 16;
        const auto feetY        = static_cast<oam::oam_t>(rawFeetY < 0 ? 0 : rawFeetY);
        OAMBuffer[0].y = feetY; OAMBuffer[1].y = feetY;
        OAMBuffer[0].x = 32;    OAMBuffer[1].x = 40;
    }
}
