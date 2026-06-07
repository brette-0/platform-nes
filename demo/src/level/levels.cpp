#include "../levels.hpp"
#include "../src/metatiles.hpp"

#include <intsh>
#include <platform-nes/technology.hpp>
using namespace br0::intsh;

namespace demo::level {
    u8 hunk_remaining;
    u16 level_data_index;
    u16 nColumns;

    u8 MetatileBuffer[14];
    u8 AttributeBuffer[8];
    u8 attr_column = 0xFF;

    const u8 TileData[] = {
    #include "../tiled/include/1-1_c"

    };

    const u8 HunkLengths[] = {
    #include "../tiled/include/1-1_s"

        , 0x00
    };

    __attribute__((always_inline))
    u8 GetNextMetaTile() {
        if (!hunk_remaining) {
            level_data_index++;
            hunk_remaining = HunkLengths[level_data_index];
        }

        hunk_remaining--;
        return TileData[level_data_index];
    }

    __attribute__((always_inline))
    u8 GetPrevMetaTile() {
        if (!hunk_remaining) {
            level_data_index--;
            hunk_remaining = HunkLengths[level_data_index];
        }

        hunk_remaining--;
        return TileData[level_data_index];
    }

    u8 GetNextWrite(const u16 step) {
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
            const u8 pal       = METATILE_ATTR(MetatileBuffer[step >> 1]);
            const u8 is_bottom = tile_row >> 1 & 1;
            const u8 shift     = attr_column & 1
                                    ? (is_bottom ? 6 : 2)
                                    : is_bottom ? 4 : 0;
            AttributeBuffer[attr_idx] |= pal << shift;
        }
        return Metatiles[MetatileBuffer[step >> 1] << 2 | step & 1];
    }

    u8 GetPrevWrite(const u16 step) {
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
            const u8 pal       = METATILE_ATTR(MetatileBuffer[step >> 1]);
            const u8 is_bottom = tile_row >> 1 & 1;
            const u8 shift     = attr_column & 1
                                    ? (is_bottom ? 4 : 0)
                                    : is_bottom ? 6 : 2;
            AttributeBuffer[attr_idx] |= pal << shift;
        }
        return Metatiles[MetatileBuffer[step >> 1] << 2 | ~step & 1];
    }

    u8 GetCurrentNext(const u16 step) {
        return Metatiles[MetatileBuffer[step >> 1] << 2 | 2 | step & 1];
    }

    u8 GetCurrentPrev(const u16 step) {
        return Metatiles[MetatileBuffer[step >> 1] << 2 | 2 | ~step & 1];
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