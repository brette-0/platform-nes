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

    const u8 TileData_1_1[] = {
    #include "../../tiled/include/1-1_st"

    };

    absolute
    const u8 HunkLengths_1_1[] = {
    #include "../../tiled/include/1-1_sl"

        , 0x00
    };

    // Dynamic plane (second Tiled layer).  No 0x00 terminator: it spans exactly
    // the same WxH cell grid as the static plane, so the static walk bounds it.
    // DynData_1_1 is the ROM master copied into the RAM pool (DynData) at load.
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
        // Point at the level data *before* sizing: BuildLevelSize() walks
        // HunkLengths to compute nColumns, so it must already be assigned.
        TileData = Levels[n].TileData;
        HunkLengths = Levels[n].HunkLengths;
        // Copy this level's dynamic run-data into the reserved RAM pool and aim
        // the dyn walkers' length source at its ROM lengths (data peeks RAM, so
        // a consumed run can be blanked in place later).
        LoadDynamicLayer(Levels[n].DynLengths, Levels[n].DynData, Levels[n].DynRuns);
        return BuildLevelSize();
    }

    // Composite the dynamic plane over the static one at the single point every
    // render path funnels through: the dynamic tile wins when non-zero, else the
    // static tile shows.  The dyn edge is advanced in lockstep with the static
    // edge so it always peeks the SAME absolute metatile.  Compositing here means
    // both the reset fill (GetNextWrite) and the scroll stream (BuildNextColumn)
    // get dynamic-over-static for free, with no separate dynamic draw pass.
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
                const u8 mask = attr_column & 1 ? 0xCC : 0x00;
                for (auto & j : AttributeBuffer)
                    j &= mask;
            }

            MetatileBuffer[step >> 1] = GetPrevMetaTile();

            const u8 tile_row  = 29 - step;
            const u8 attr_idx  = tile_row >> 2;
            const u8 pal       = Metatiles_ATTR[MetatileBuffer[step >> 1]] & MetatilePaletteMask;
            const u8 is_bottom = tile_row >> 1 & 1;
            const u8 shift     = attr_column & 1
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

    // Right-scroll: expand the 14 metatiles of the entering column into the 56
    // nametable tiles + 8 attribute bytes.  The metatiles are read straight out of
    // the collision window (ColMapColumn) -- the SAME composited, coin-aware column
    // ColMapTrack already decoded this scroll -- instead of re-walking the RLE here.
    // This deletes the second RLE walk that used to run on every column-crossing
    // frame (the render path and the collision producer were both decompressing the
    // same column); the window is now the single decompressor and render just does
    // the CHR expansion it alone needs.  `worldCol` is the absolute metatile column
    // the NMI will display at the right edge (derived from lastXWorldSpace by the
    // caller, matching the NMI's nametable placement exactly).
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

    // Left-scroll mirror: same window read, written bottom-up.  Placement matches the
    // old reversed strides exactly -- top row at buf[54-step]/buf[55-step], bottom row
    // at buf[26-step]/buf[27-step].  The old GetPrevMetaTile walked edgeL UP the column
    // (row 13 -> 0), so to reproduce that order from the window we index col[13 - mt];
    // combined with the reversed buf placement this renders the column upright, exactly
    // as the right path does forward.
    void BuildPrevColumn(u8* buf, const u16 worldCol) {
        attr_column++;
        const u8 mask = attr_column & 1 ? 0xCC : 0x00;
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
            const u8 shift     = attr_column & 1 ? (is_bottom ? 4 : 0)
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