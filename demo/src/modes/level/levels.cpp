#include "levels.hpp"
#include "collision_map.hpp"   // ColMapColumn: read the composited column the window already built
#include "../../graphics/metatiles.hpp"
#include "../../banks.hpp"     // ACTORS, level_graphics_tag
#include <platform-nes/mappers/mmc3.hpp>  // mmc3::CallInBlock

#include <intsh>
#include <platform-nes/technology.hpp>

#include "types.hpp"
using namespace br0::intsh;

namespace level {
    Cursor edgeR;
    Cursor edgeL;
    u16 nColumns;

    const u8* TileData;   // static plane: flat column-major ROM array, see types.hpp

    u8 MetatileBuffer[14];
    u8 AttributeBuffer[8];
    u8 attr_column = 0xFF;

    // pal is a 2-bit palette index (MetatilePaletteMask) and shift is always one
    // of {0,2,4,6} (an attribute-byte nibble position), so `pal << shift` only
    // ever takes 16 distinct values. The shift amount isn't a compile-time
    // constant at any one call site (it's chosen by a runtime parity/half
    // branch), so `pal << shift` as written compiles to a call to __ashlqi3 --
    // a loop of up to 6 ASLs plus call overhead -- once per metatile row.
    // Indexing this instead is a handful of cycles regardless of shift amount.
    static constexpr u8 kPalShifted[4][4] = {
        { 0,  0,   0,   0 },
        { 1,  4,  16,  64 },
        { 2,  8,  32, 128 },
        { 3, 12,  48, 192 },
    };

    // BuildNextColumn/BuildPrevColumn walk a fixed 14-row column with
    // tile_row = 2+2*mt (Next) / 29-2*mt (Prev), so attr_idx (tile_row>>2) is
    // fully determined by the loop counter mt -- not something that needs
    // recomputing (add + 2 shifts) every row. Likewise is_bottom (tile_row>>1&1)
    // reduces to a plain function of mt's parity (Next: 1 on even mt, 0 on odd;
    // Prev: 0 on even mt, 1 on odd), so the two shift-index values a call can
    // ever produce are known before the loop starts, not per row. Tabling
    // attr_idx and hoisting the shift-index pair out of the loop turns each
    // row's bookkeeping into one indexed load + one already-computed pick,
    // instead of an add, three shifts, and a nested branch every iteration.
    static constexpr u8 kNextAttrIdx[14] = {0,1,1,2,2,3,3,4,4,5,5,6,6,7};
    static constexpr u8 kPrevAttrIdx[14] = {7,6,6,5,5,4,4,3,3,2,2,1,1,0};

    // --- Attribute-half parity correction for odd-width viewports -------------
    // One NES attribute byte spans 32px = two metatile columns (a left/right
    // nibble pair), so a column's half is its absolute metatile-column parity.
    // The streaming composer tracks that parity with the monotonic `attr_column`
    // counter, whose phase is fixed at init to (attr_column - cameraCol) ==
    // viewport_mx() and is invariant under every build (forward: both +1;
    // backward: +1/-1, i.e. +2). Forward (Next) composition is therefore always
    // correct. The backward (Prev) composer writes the INVERTED half, which stays
    // phase-correct across a direction reversal only when the viewport spans a
    // whole number of attribute bytes -- an EVEN metatile-column count, i.e.
    // viewport_tx() a multiple of 4. The GBA panel is 240px = 30 tiles = 15
    // metatiles (odd), so a right->left reversal injects a one-column parity
    // offset and the returned-over columns land in the wrong half (grey coins,
    // visible only at palette boundaries near the level start). This constant
    // re-flips the Prev parity by exactly that offset: it is 0 for every
    // multiple-of-4 viewport (NES/DS 32, Switch/Wii U 52, SDL `&~3`) and 1 only
    // for the GBA's 30. Next is left untouched -- it has no reversal offset.
    //
    // A constexpr *function*, not a constexpr variable: on the SDL/desktop
    // (LANDSCAPE) targets viewport_tx() reads runtime globals (mode/scale), so it
    // is never a constant expression -- a constexpr variable initialiser would not
    // compile there. As a function it folds to a compile-time 0/1 on the
    // fixed-viewport targets and is a cheap runtime call (always 0, since SDL's
    // `& ~3u` keeps viewport_mx() even) on the runtime-sized ones, mirroring how
    // viewport_tx() itself is constexpr-yet-runtime.
    static constexpr u8 prev_parity_fix() { return (video::viewport_tx() >> 1) & 1u; }

    // LEVEL_DATA: static plane + DynLengths, read CONTINUOUSLY -- see
    // banks.hpp's ::LevelDataBank.
    LEVEL_DATA const u8 TileData_1_1[] = {
        #include "../../../tiled/include/1-1_st"
    };

    // LEVEL_DYNAMIC, not LEVEL_DATA: read ONCE at load -- see
    // banks.hpp's ::LevelDynamicBank.
    LEVEL_DYNAMIC const u8 DynData_1_1[] = {
        #include "../../../tiled/include/1-1_dt"
    };

    LEVEL_DATA const u8 DynLengths_1_1[] = {
        #include "../../../tiled/include/1-1_dl"
    };

    const Level Levels[] = {
        {TileData_1_1, sizeof(TileData_1_1) / levelHeight,
         DynData_1_1, DynLengths_1_1, sizeof(DynLengths_1_1)}
    };

    // LEVEL_CODE, in window 1 -- see banks.hpp. NI: sole call site is
    // level.cpp's CallInBlock<level_code_tag>, so without it LTO inlines this
    // into the FIXED trampoline instead of LEVEL_CODE -- same failure mode as
    // EnterLevelSetup's own NI.
    NI LEVEL_CODE bool LoadLevel(const u16 n) {
        // TileData holds the active level's STATIC PLANE (walls/ground).
        TileData = Levels[n].TileData;
        nColumns = Levels[n].nColumns;

        // Window 2's bank is a RUNTIME choice keyed by level index, not a
        // compile-time tag -- see banks.hpp's ::LevelDynamicBank/
        // ::LevelDataBank. RAW mmc3::SwitchBank, not mmc3::CallInBlock<Tag>:
        // there is nothing meaningful to restore window 2 TO here. This is
        // level entry, establishing window 2's content for the whole
        // session, not a nested call handing control back to a caller that
        // needs its own prior window 2 state preserved.
        mmc3::SwitchBank(mmc3::window2Control, LevelDynamicBank(n));
        LoadDynamicLayer(Levels[n].DynLengths, Levels[n].DynData, Levels[n].DynRuns);

        // Settle window 2 on the level's STATIC data for the rest of the
        // session: TileData/DynLengths are read every frame via plain
        // pointers, never through a farcall, so window 2 must stay here,
        // untouched, until the level ends.
        mmc3::SwitchBank(mmc3::window2Control, LevelDataBank(n));
        return true;
    }

    AI
    u8 GetNextMetaTile() {
        const u8 s = edgeR.Fetch();
        const u8 d = dynEdgeR.Fetch();
        edgeR.Move(1);
        dynEdgeR.Move(1);
        return d ? d : s;
    }

    AI
    u8 GetPrevMetaTile() {
        const u8 s = edgeL.Fetch();
        const u8 d = dynEdgeL.Fetch();
        edgeL.Move(-1);
        dynEdgeL.Move(-1);
        return d ? d : s;
    }

    // NOT AI any more: the body now crosses a window-2 farcall boundary
    // (::level_graphics_tag), so it can't be inlined at the caller regardless
    // -- these are only ever reached through a function pointer
    // (ppu::WriteFromProviderToNameTable) anyway, which already ruled out
    // real inlining. One-time cost only: sole call site is EnterLevelSetup's
    // initial column prefill (level.cpp), never touched again after level
    // entry, so the extra switch per tile here is free.
    u8 GetNextWrite(const u8 step) {
        u8 m;
        if (~step & 1) {
            if (step == 0) {
                attr_column++;
                const u8 mask = attr_column & 1 ? 0x33 : 0x00;
                for (auto & j : AttributeBuffer)
                    j &= mask;
            }
            // GetNextMetaTile reads TileData/DynLengths -- LevelDataBank,
            // ambient window 2 -- so it runs OUTSIDE the level_graphics block.
            m = GetNextMetaTile();
            MetatileBuffer[step >> 1] = m;
        } else {
            m = MetatileBuffer[step >> 1];
        }
        return CallLevelGraphics([&]() -> u8 {
            if (~step & 1) {
                const u8 tile_row  = 2 + step;
                const u8 attr_idx  = tile_row >> 2;
                const u8 pal       = Metatiles_ATTR[m] & MetatilePaletteMask;
                const u8 is_bottom = tile_row >> 1 & 1;
                const u8 shift     = attr_column & 1
                                        ? (is_bottom ? 6 : 2)
                                        : is_bottom ? 4 : 0;
                AttributeBuffer[attr_idx] |= kPalShifted[pal][shift >> 1];
            }
            return step & 1 ? Metatiles_UR[m] : Metatiles_UL[m];
        });
    }

    u8 GetPrevWrite(const u8 step) {
        u8 m;
        if (~step & 1) {
            if (step == 0) {
                attr_column++;
                const u8 mask = (attr_column + prev_parity_fix()) & 1 ? 0xCC : 0x00;
                for (auto & j : AttributeBuffer)
                    j &= mask;
            }
            // GetPrevMetaTile: ambient window 2 (LevelDataBank), same
            // reasoning as GetNextWrite above.
            m = GetPrevMetaTile();
            MetatileBuffer[step >> 1] = m;
        } else {
            m = MetatileBuffer[step >> 1];
        }
        return CallLevelGraphics([&]() -> u8 {
            if (~step & 1) {
                const u8 tile_row  = 29 - step;
                const u8 attr_idx  = tile_row >> 2;
                const u8 pal       = Metatiles_ATTR[m] & MetatilePaletteMask;
                const u8 is_bottom = tile_row >> 1 & 1;
                const u8 shift     = (attr_column + prev_parity_fix()) & 1
                                        ? (is_bottom ? 4 : 0)
                                        : is_bottom ? 6 : 2;
                AttributeBuffer[attr_idx] |= kPalShifted[pal][shift >> 1];
            }
            return step & 1 ? Metatiles_UL[m] : Metatiles_UR[m];
        });
    }

    // GetCurrentNext/GetCurrentPrev only ever read MetatileBuffer (RAM,
    // already filled by GetNextWrite/GetPrevWrite's own pass over the same
    // step) and the metatile planes -- no window-2/LevelData dependency, so
    // the whole body is one level_graphics_tag block.
    u8 GetCurrentNext(const u8 step) {
        return CallLevelGraphics([&]() -> u8 {
            const u8 m = MetatileBuffer[step >> 1];
            return step & 1 ? Metatiles_BR[m] : Metatiles_BL[m];
        });
    }

    u8 GetCurrentPrev(const u8 step) {
        return CallLevelGraphics([&]() -> u8 {
            const u8 m = MetatileBuffer[step >> 1];
            return step & 1 ? Metatiles_BL[m] : Metatiles_BR[m];
        });
    }

    // ACTORS: sole caller is ProcessMovement (player.cpp), now in the actors
    // bank -- see banks.hpp's ::actor_tag comment. col[] comes from ColMapColumn
    // (ViewMap, RAM) -- no window-2/LevelData dependency anywhere in this
    // function, so the metatile-table portion is ONE level_graphics_tag block
    // rather than per-row: only fires on a scroll-column boundary crossing
    // (lastXWorldSpace hysteresis in player.cpp), not every frame.
    ACTORS void BuildNextColumn(u8* buf, const u16 worldCol) {
        attr_column++;
        const u8 mask = attr_column & 1 ? 0x33 : 0x00;
        for (auto & j : AttributeBuffer) j &= mask;

        const u8* col = ColMapColumn(worldCol);
        if (!col) return;                            // out of window (never in steady scroll)

        // mt even -> is_bottom=1, mt odd -> is_bottom=0 (see kNextAttrIdx comment).
        const u8 shiftIdxEven = attr_column & 1 ? 3 : 2;
        const u8 shiftIdxOdd  = attr_column & 1 ? 1 : 0;

        CallLevelGraphics([&] {
            for (u8 mt = 0; mt < 14; ++mt) {
                const u8 m   = col[mt];                  // top-to-bottom, slot s row mt
                const u8 pal = Metatiles_ATTR[m] & MetatilePaletteMask;
                const u8 shiftIdx = mt & 1 ? shiftIdxOdd : shiftIdxEven;
                AttributeBuffer[kNextAttrIdx[mt]] |= kPalShifted[pal][shiftIdx];

                const u8 step      = mt << 1;             // even tile-step: 0,2,..,26
                buf[step]          = Metatiles_UL[m];
                buf[step + 1]      = Metatiles_UR[m];
                buf[28 + step]     = Metatiles_BL[m];
                buf[28 + step + 1] = Metatiles_BR[m];
            }
        });
    }

    // ACTORS: see BuildNextColumn's comment.
    ACTORS void BuildPrevColumn(u8* buf, const u16 worldCol) {
        attr_column++;
        const u8 ap = (attr_column + prev_parity_fix()) & 1;   // odd-width parity fix
        const u8 mask = ap ? 0xCC : 0x00;
        for (auto & j : AttributeBuffer) j &= mask;

        const u8* col = ColMapColumn(worldCol);
        if (!col) return;                            // out of window (never in steady scroll)

        // mt even -> is_bottom=0, mt odd -> is_bottom=1 (see kPrevAttrIdx comment).
        const u8 shiftIdxEven = ap ? 0 : 1;
        const u8 shiftIdxOdd  = ap ? 2 : 3;

        CallLevelGraphics([&] {
            for (u8 mt = 0; mt < 14; ++mt) {
                const u8 m   = col[13 - mt];              // bottom-to-top, mirroring edgeL's walk
                const u8 pal = Metatiles_ATTR[m] & MetatilePaletteMask;
                const u8 shiftIdx = mt & 1 ? shiftIdxOdd : shiftIdxEven;
                AttributeBuffer[kPrevAttrIdx[mt]] |= kPalShifted[pal][shiftIdx];

                const u8 step  = mt << 1;
                buf[54 - step] = Metatiles_UL[m];
                buf[55 - step] = Metatiles_UR[m];
                buf[26 - step] = Metatiles_BL[m];
                buf[27 - step] = Metatiles_BR[m];
            }
        });
    }

}