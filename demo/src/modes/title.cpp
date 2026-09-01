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

    // Nametable 0 holds the level preview (a crop, not the whole level --
    // see DrawLevelPreview). Nametable 1 holds the menu, at the
    // horizontally-adjacent page ($2400) -- kMenuNT is that page's start in
    // tile-coordinate write-address space (xy_to_nt_addr/xy_to_at_addr's
    // x>=32 branch, src/nes/video.cpp). These are genuinely independent
    // physical pages on this project's build (ALTERNATIVE_NAMETABLE=1,
    // local.cmake, every target -- a four-screen board: 2 KiB console CIRAM
    // + 2 KiB cartridge VRAM, none of it mirrored), so no
    // mmc3::SetMirroring call is needed to separate them.
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
        // Four-screen board: clears all 4 nametable/attribute pages,
        // nametable 1 (the menu) included.
        ppu::Flush(chrHUDWhitespace_tile, 0xff);
        // BG palette 3, not 0: palette 0 is the level's own (DrawLevelPreview
        // writes BGColours there, so terrain/air render in their real
        // colours instead of white-on-black text contrast) -- see
        // MenuAttributesProvider, which now points menu text at palette 3
        // instead. Offset 12 (colour 0) is left unwritten: titleScreenColours
        // has no entry for it at all -- transparency/backdrop is decided
        // solely by the level's own BGColours (DrawLevelPreview, below),
        // never by the title screen.
        ppu::pal::WriteFromBuffer(13, titleScreenColours, 3);

        DrawLevelPreview();
        InitTitleScreen();

        ppu::SetScroll(0, 0xff);
        ppu::EnableRendering(ppu::ctrl::SPRITE_ADDR | ppu::ctrl::SPRITE_SIZE | ppu::ctrl::GEN_NMI, ppu::mask::BG_L | ppu::mask::SPRITE_L);

        // ApplySplit rides MMC3's scanline IRQ. The CPU boots with
        // interrupts masked (SEI); only level.cpp unmasks them today, so
        // without this, ScheduleScanlineIRQ arms the counter every frame but
        // it's never serviced -- no split, level fills the whole screen.
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

            // Every iteration reaches WaitForPresent exactly once, regardless
            // of which branch above ran -- on SDL3 that's what pumps the OS
            // event queue, paces to 60Hz, and presents the frame. Skipping it
            // on the idle (no input) path -- as this loop used to -- leaves
            // desktop targets spinning with no event pump, which reads to the
            // OS as a hung window. NES/console backends don't need the pump
            // but still want the one-call-per-frame NMI/present pacing.
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
        ppu::WriteSingleToNameTable(menuCol - 2, menuRow1 + lastMenuOption, chrEmpty_tile);
        // write chrArrow_tile where arrow now is
        ppu::WriteSingleToNameTable(menuCol - 2, menuRow1 + menuOption, chrArrow_tile);
        lastMenuOption = menuOption;

        // re-DMA OAM every frame -- OAM decays if it isn't refreshed
        // regularly, and now that sprites are enabled that matters here too.
        oam::RefreshSprites(OAMBuffer);

        // Nametable 0's own row 0, always -- DrawLevelPreview already wrote
        // the correct (bottom-aligned) slice there, so no runtime scroll
        // offset is needed. Keeping this at a constant 0 is what lets
        // ApplySplit's crossing stay a plain, safe $2005 write -- see its
        // own comment.
        ppu::SetScroll(0, 0);

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

    // Crosses from nametable 0 (the level preview) into nametable 1's (the
    // menu's) matching row, horizontally. Safe as a plain ppu::SetScroll
    // ($2005) write -- unlike an earlier version of this, which scrolled
    // nametable 0 at runtime to bottom-align the level and needed a $2006
    // (PPUADDR) write here instead. The reason: mid-frame, the PPU only
    // ever transfers the HORIZONTAL half of the scroll (coarse X, and a
    // horizontal crossing's nametable bit) into its active render address,
    // via the automatic dot-257 transfer every scanline -- coarse Y is
    // never part of that transfer, so it just keeps counting up from
    // wherever the frame's scroll started it. nmi_handler now always starts
    // at row 0 (DrawLevelPreview crops the level to the visible band AT
    // WRITE TIME instead), so coarse Y is naturally still exactly
    // SplitRow() here -- safely inside the valid 0-29 range -- with no
    // dependency on cycle-exact write timing the way $2006 needed.
    // 256 sets coarse X/fine X to 0 (nametable 1's own column 0, no shift)
    // while flipping the horizontal nametable bit.
    static void ApplySplit() {
        ppu::SetScroll(kMenuNT << 3, static_cast<u16>(SplitRow()) << 3);
    }

    void InitTitleScreen() {
        // Menu content lives in nametable 1 (the horizontally-adjacent
        // page, kMenuNT tile-columns over), at the SAME on-screen row
        // numbers the split lands on (SplitRow()-relative) -- see
        // ApplySplit's comment: the crossing only ever changes which table
        // X points into, never the row, so nametable 1 has to hold its
        // content at the row it'll actually be displayed on.
        const u8 splitRow  = SplitRow();
        const u8 menuRows  = static_cast<u8>((viewport_my() << 1) - splitRow);
        // Ceiling-divide, not viewport_mx()>>1 (== viewport_tx()>>2, floor):
        // every other backend's viewport width is a multiple of 4 tiles, but
        // the 3DS's fixed 50-tile viewport isn't (50/4 = 12.5) -- floor
        // division under-covers by one cell, leaving the trailing partial
        // attribute column (the last 2 tile-columns) at Flush's default
        // palette 3 instead of this band's palette 0.
        const u8 attrCells = static_cast<u8>((video::viewport_tx() + 3) >> 2);
        for (u8 r = 0; r < menuRows; r += 4) {
            ppu::WriteFromProviderToAttributeTable(
                kMenuNT, splitRow + r,
                MenuAttributesProvider, attrCells, 0
            );
        }

        ui::text::DrawText(
            SIZED_OBJ(msg_title),
            {kMenuNT + 1, static_cast<u16>(splitRow + 1)},
            {static_cast<u8>((viewport_mx() >> 1) - 1), 3}, chrHUDWhitespace_tile,
            ui::text::Alignment::Left
        );

        // menu items -- all start at the same column, with a 1-tile gap from
        // the right edge for the longest entry (msg_newGame/msg_continue).
        const u16 menuCol = kMenuNT + (viewport_mx() << 1) - 1 - sizeof(msg_continue);

        // selection cursor -- starts on New Game, one tile of gap before the text.
        ppu::WriteSingleToNameTable(menuCol - 2, splitRow + 1, chrArrow_tile);

        ppu::WriteFromBufferToNameTable(menuCol, splitRow + 1, SIZED_OBJ(msg_newGame), 0);
        ppu::WriteFromBufferToNameTable(menuCol, splitRow + 2, SIZED_OBJ(msg_continue), 0);
        ppu::WriteFromBufferToNameTable(menuCol, splitRow + 3, SIZED_OBJ(msg_options), 0);
#if defined(TARGET_MACOS) || defined(TARGET_WINDOWS) || defined(TARGET_LINUX)
        // consoles have no OS to quit back to -- PC targets only.
        ppu::WriteFromBufferToNameTable(menuCol, splitRow + 4, SIZED_OBJ(msg_quit), 0);
#endif
    }

    u8 MenuAttributesProvider(const u8 i) {
        // All 4 quadrants -> palette 3 (0b11 in each 2-bit field): title's
        // own text colours, see main(). Not 0x03 -- that would only cover
        // the top-left quadrant, leaving the other three at palette 0.
        return 0xff;
    }

    static oam::oam_t Clear(const u16) {
        return 0xf0;
    }

    // Paints a bottom-aligned crop of the level into nametable 0, starting
    // at its own row 0: whatever fits above the split (SplitRow() rows),
    // taken from the BOTTOM of the level so its last metatile row (the
    // ground, in this alpha level) ends up directly above the menu instead
    // of the level's own top (empty sky, in this alpha level). Cropped and
    // positioned at WRITE TIME rather than via a runtime scroll offset --
    // see ApplySplit's comment for why that specifically matters (it's what
    // keeps the split a plain, safe $2005 write). No HUD: EnterLevelSetup's
    // real HUD only exists to label a working game world above it, and it
    // doesn't fit in the cropped band without pushing the ground back out
    // of view.
    //
    // No Actor, no physics, no scroll, no collision, no per-frame update --
    // a preview, not level mode. Runs once, before EnableRendering, so every
    // write below is a plain direct VRAM poke (rendering is off, nothing to
    // tear).
    //
    // Reuses level's own loader/graphics rather than re-deriving any of it:
    // LoadLevel populates level::TileData/nColumns and the RAM-mirrored
    // Metatiles_* planes (metatiles.hpp), and leaves window 2 parked on
    // LevelDataBank(0) -- exactly the state level.cpp's own streaming code
    // depends on -- so TileData reads below are ordinary ambient loads, no
    // farcall per tile. CallLevelGraphics (banks.hpp) borrows window 2 for
    // msMary/maryColors/BGColours the same way EnterLevelSetup does.
    //
    // Composites the dynamic plane over the static one (non-zero wins), the
    // same rule GetNextMetaTile uses for the real level's initial screen
    // fill (levels.cpp) -- level 1's dynamic layer isn't sparse decoration,
    // it's solid coin tiles across the whole sky, so without this the
    // preview is nearly all air. DynLengths/DynData are already ROM->RAM
    // loaded by LoadLevel above; a fresh DynamicCursor here (not the global
    // dynEdgeR) keeps this walk independent of whatever state real level
    // entry expects to seed later.
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
                // Metatiles_UR/Metatiles_BL are swapped versus their names --
                // metatiles.cpp's kMetatiles_BL_rom/kMetatiles_UR_rom pull
                // from MetatilePlane(1)/(2), but MetatilesSrc's own row
                // layout (and MT_SPLIT) is {UL, UR, BL, BR, AT}, so plane 1
                // is really UR-source data and plane 2 is really BL-source
                // data. levels.cpp's GetNextWrite/GetCurrentNext (the real
                // level's column fill) write Metatiles_UR to the
                // bottom-left tile and Metatiles_BL to the top-right tile,
                // which is what actually cancels the swap out on screen --
                // matched here rather than the array names, or this preview
                // renders every asymmetric metatile with its right/left
                // halves crossed.
                ppu::WriteSingleToNameTable(tx,     ty,     Metatiles_UL[mL]);
                ppu::WriteSingleToNameTable(tx + 1, ty,     Metatiles_BL[mL]);
                ppu::WriteSingleToNameTable(tx,     ty + 1, Metatiles_UR[mL]);
                ppu::WriteSingleToNameTable(tx + 1, ty + 1, Metatiles_BR[mL]);
                ppu::WriteSingleToNameTable(tx + 2, ty,     Metatiles_UL[mR]);
                ppu::WriteSingleToNameTable(tx + 3, ty,     Metatiles_BL[mR]);
                ppu::WriteSingleToNameTable(tx + 2, ty + 1, Metatiles_UR[mR]);
                ppu::WriteSingleToNameTable(tx + 3, ty + 1, Metatiles_BR[mR]);
            }
            for (u8 br = 0; br < visibleMetaRows; br += 2) {
                const u8 palTL = Metatiles_ATTR[colBuf[0][br]]     & MetatilePaletteMask;
                const u8 palTR = Metatiles_ATTR[colBuf[1][br]]     & MetatilePaletteMask;
                const u8 palBL = Metatiles_ATTR[colBuf[0][br + 1]] & MetatilePaletteMask;
                const u8 palBR = Metatiles_ATTR[colBuf[1][br + 1]] & MetatilePaletteMask;
                const u8 attrByte = static_cast<u8>(palTL | (palTR << 2) | (palBL << 4) | (palBR << 6));
                ppu::WriteSingleToAttributeTable(tx, static_cast<u16>(2 * br), attrByte);
            }
        }

        // Player: msMary's tile/attribute pair is the same idle pose
        // EnterLevelSetup seeds a real level with -- reused verbatim, just
        // parked at a fixed screen position instead of being driven by an
        // Actor. Real spawn positioning isn't implemented yet (Player::Reset
        // doesn't touch actor.screen), so this stands on the ground instead
        // of trying to match an as-yet-nonexistent spawn point. OAM slots
        // 0/1 are free: title never draws any sprites of its own.
        //
        // Also loads the level's own BG palettes: 0 (air/terrain -- all 4
        // bytes, including its own colour 0, which is what actually
        // establishes the shared $3F00 backdrop now) and 1's colours 1-3
        // (skipping its own colour-0 byte -- redundant with palette 0's,
        // same underlying $3F00 register either way). Title's own text
        // moved to palette 3 (main()) specifically so palette 0 could be
        // freed for this -- previously title's white-on-black palette 0
        // doubled as terrain's palette too (metatiles.cpp puts air/terrain
        // in palette group 0), so terrain rendered flat white instead of
        // its real colours.
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
