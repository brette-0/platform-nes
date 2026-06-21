#include "levels.hpp"
#include "collision_map.hpp"   // ColMapColumn: read the composited column the window already built
#include "../graphics/metatiles.hpp"

#include <intsh>
#include <platform-nes/technology.hpp>

#include "types.hpp"
using namespace br0::intsh;

namespace demo::level {
    Cursor edgeR;
    Cursor edgeL;
    u16 nColumns;

    const u8* TileData;
    const u8* HunkLengths;

    u8 MetatileBuffer[14];
    u8 AttributeBuffer[8];
    u8 attr_column = 0xFF;

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

    const u8 TileData_1_1[] = {
        #include "../../tiled/include/1-1_st"
    };

    absolute
    const u8 HunkLengths_1_1[] = {
    #include "../../tiled/include/1-1_sl"
        , 0x00
    };

    const u8 DynData_1_1[] = {
        #include "../../tiled/include/1-1_dt"
    };

    const u8 DynLengths_1_1[] = {
        #include "../../tiled/include/1-1_dl"
    };

    const Level Levels[] = {
        {TileData_1_1, HunkLengths_1_1,
         DynData_1_1, DynLengths_1_1, sizeof(DynLengths_1_1)}
    };

    bool LoadLevel(const u16 n) {
        TileData = Levels[n].TileData;
        HunkLengths = Levels[n].HunkLengths;
        LoadDynamicLayer(Levels[n].DynLengths, Levels[n].DynData, Levels[n].DynRuns);
        return BuildLevelSize();
    }

    __attribute__((always_inline))
    u8 GetNextMetaTile() {
        const u8 s = edgeR.Fetch();
        const u8 d = dynEdgeR.Fetch();
        edgeR.Move(1);
        dynEdgeR.Move(1);
        return d ? d : s;
    }

    __attribute__((always_inline))
    u8 GetPrevMetaTile() {
        const u8 s = edgeL.Fetch();
        const u8 d = dynEdgeL.Fetch();
        edgeL.Move(-1);
        dynEdgeL.Move(-1);
        return d ? d : s;
    }

    __attribute__((always_inline))
    u8 GetNextWrite(const u8 step) {
        if (~step & 1) {
            if (step == 0) {
                attr_column++;
                const u8 mask = attr_column & 1 ? 0x33 : 0x00;
                for (auto & j : AttributeBuffer)
                    j &= mask;
            }

            MetatileBuffer[step >> 1] = GetNextMetaTile();

            const u8 tile_row  = 2 + step;
            const u8 attr_idx  = tile_row >> 2;
            const u8 pal       = Metatiles_ATTR[MetatileBuffer[step >> 1]] & MetatilePaletteMask;
            const u8 is_bottom = tile_row >> 1 & 1;
            const u8 shift     = attr_column & 1
                                    ? (is_bottom ? 6 : 2)
                                    : is_bottom ? 4 : 0;
            AttributeBuffer[attr_idx] |= pal << shift;
        }
        const u8 m = MetatileBuffer[step >> 1];
        return step & 1 ? Metatiles_UR[m] : Metatiles_UL[m];
    }

    __attribute__((always_inline))
    u8 GetPrevWrite(const u8 step) {
        if (~step & 1) {
            if (step == 0) {
                attr_column++;
                const u8 mask = (attr_column + prev_parity_fix()) & 1 ? 0xCC : 0x00;
                for (auto & j : AttributeBuffer)
                    j &= mask;
            }

            MetatileBuffer[step >> 1] = GetPrevMetaTile();

            const u8 tile_row  = 29 - step;
            const u8 attr_idx  = tile_row >> 2;
            const u8 pal       = Metatiles_ATTR[MetatileBuffer[step >> 1]] & MetatilePaletteMask;
            const u8 is_bottom = tile_row >> 1 & 1;
            const u8 shift     = (attr_column + prev_parity_fix()) & 1
                                    ? (is_bottom ? 4 : 0)
                                    : is_bottom ? 6 : 2;
            AttributeBuffer[attr_idx] |= pal << shift;
        }
        const u8 m = MetatileBuffer[step >> 1];
        return step & 1 ? Metatiles_UL[m] : Metatiles_UR[m];
    }

    __attribute__((always_inline))
    u8 GetCurrentNext(const u8 step) {
        const u8 m = MetatileBuffer[step >> 1];
        return step & 1 ? Metatiles_BR[m] : Metatiles_BL[m];
    }

    __attribute__((always_inline))
    u8 GetCurrentPrev(const u8 step) {
        const u8 m = MetatileBuffer[step >> 1];
        return step & 1 ? Metatiles_BL[m] : Metatiles_BR[m];
    }

    void BuildNextColumn(u8* buf, const u16 worldCol) {
        attr_column++;
        const u8 mask = attr_column & 1 ? 0x33 : 0x00;
        for (auto & j : AttributeBuffer) j &= mask;

        const u8* col = ColMapColumn(worldCol);
        if (!col) return;                            // out of window (never in steady scroll)

        for (u8 mt = 0; mt < 14; ++mt) {
            const u8 m = col[mt];                    // top-to-bottom, slot s row mt

            const u8 step      = mt << 1;            // even tile-step: 0,2,..,26
            const u8 tile_row  = 2 + step;
            const u8 attr_idx  = tile_row >> 2;
            const u8 pal       = Metatiles_ATTR[m] & MetatilePaletteMask;
            const u8 is_bottom = tile_row >> 1 & 1;
            const u8 shift     = attr_column & 1 ? (is_bottom ? 6 : 2)
                                                 : (is_bottom ? 4 : 0);
            AttributeBuffer[attr_idx] |= pal << shift;

            buf[step]          = Metatiles_UL[m];
            buf[step + 1]      = Metatiles_UR[m];
            buf[28 + step]     = Metatiles_BL[m];
            buf[28 + step + 1] = Metatiles_BR[m];
        }
    }

    void BuildPrevColumn(u8* buf, const u16 worldCol) {
        attr_column++;
        const u8 ap = (attr_column + prev_parity_fix()) & 1;   // odd-width parity fix
        const u8 mask = ap ? 0xCC : 0x00;
        for (auto & j : AttributeBuffer) j &= mask;

        const u8* col = ColMapColumn(worldCol);
        if (!col) return;                            // out of window (never in steady scroll)

        for (u8 mt = 0; mt < 14; ++mt) {
            const u8 m = col[13 - mt];               // bottom-to-top, mirroring edgeL's walk

            const u8 step      = mt << 1;
            const u8 tile_row  = 29 - step;
            const u8 attr_idx  = tile_row >> 2;
            const u8 pal       = Metatiles_ATTR[m] & MetatilePaletteMask;
            const u8 is_bottom = tile_row >> 1 & 1;
            const u8 shift     = ap ? (is_bottom ? 4 : 0)
                                    : (is_bottom ? 6 : 2);
            AttributeBuffer[attr_idx] |= pal << shift;

            buf[54 - step] = Metatiles_UL[m];
            buf[55 - step] = Metatiles_UR[m];
            buf[26 - step] = Metatiles_BL[m];
            buf[27 - step] = Metatiles_BR[m];
        }
    }

    MINSIZE bool BuildLevelSize() {
        u8 temp   = 0;
        nColumns = 0;

        for (u16 i = 0; i < 0xffff; i++) {
            if (HunkLengths[i] == 0)
                return true;

            temp += HunkLengths[i];

            while (temp >= levelHeight) {
                nColumns++;
                temp -= levelHeight;
            }
        }

        return false;
    }
}