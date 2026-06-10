#include "levels.hpp"
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
    #include "../../tiled/include/1-1_c"

    };

    const u8 HunkLengths_1_1[] = {
    #include "../../tiled/include/1-1_s"

        , 0x00
    };

    const Level Levels[] = {
        {TileData_1_1, HunkLengths_1_1}
    };

    bool LoadLevel(const u16 n) {
        // Point at the level data *before* sizing: BuildLevelSize() walks
        // HunkLengths to compute nColumns, so it must already be assigned.
        TileData = Levels[n].TileData;
        HunkLengths = Levels[n].HunkLengths;
        return BuildLevelSize();
    }

    __attribute__((always_inline))
    u8 GetNextMetaTile() {
        const u8 v = edgeR.Fetch();
        edgeR.Move(1);
        return v;
    }

    __attribute__((always_inline))
    u8 GetPrevMetaTile() {
        const u8 v = edgeL.Fetch();
        edgeL.Move(-1);
        return v;
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
            const u8 pal       = Metatiles_ATTR[MetatileBuffer[step >> 1]];
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
            const u8 pal       = Metatiles_ATTR[MetatileBuffer[step >> 1]];
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

    // Right-scroll: one pass over the 14 metatiles of the column.  Each metatile
    // is fetched once and its four CHR corners are written straight from the
    // split planes -- buf[step], buf[step+1] (top row) and buf[28+step],
    // buf[28+step+1] (bottom row).  Indexing each plane by the raw metatile id
    // compiles to a single `lda Metatiles_xx,x`, replacing the old 16-bit
    // `Metatiles[id<<2|corner]` index and the per-tile step&1 branch.
    void BuildNextColumn(u8* buf) {
        attr_column++;
        const u8 mask = attr_column & 1 ? 0x33 : 0x00;
        for (auto & j : AttributeBuffer) j &= mask;

        for (u8 mt = 0; mt < 14; ++mt) {
            const u8 m = GetNextMetaTile();

            const u8 step      = mt << 1;            // even tile-step: 0,2,..,26
            const u8 tile_row  = 2 + step;
            const u8 attr_idx  = tile_row >> 2;
            const u8 pal       = Metatiles_ATTR[m];
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

    // Left-scroll mirror: walks the backward edge, writing the column bottom-up.
    // Placement matches the old reversed PopulateFromProvider strides exactly --
    // top row at buf[54-step]/buf[55-step], bottom row at buf[26-step]/buf[27-step].
    void BuildPrevColumn(u8* buf) {
        attr_column++;
        const u8 mask = attr_column & 1 ? 0xCC : 0x00;
        for (auto & j : AttributeBuffer) j &= mask;

        for (u8 mt = 0; mt < 14; ++mt) {
            const u8 m = GetPrevMetaTile();

            const u8 step      = mt << 1;
            const u8 tile_row  = 29 - step;
            const u8 attr_idx  = tile_row >> 2;
            const u8 pal       = Metatiles_ATTR[m];
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