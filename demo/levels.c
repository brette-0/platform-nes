#include "levels.h"
#include "metatiles.h"

#include <stdint.h>

uint8_t hunk_remaining;
uint16_t level_data_index;

uint8_t MetatileBuffer[14];
uint8_t AttributeBuffer[8];
uint8_t attr_column = 0xFF;

const uint8_t LevelData[] = {
#include "tiled/include/1-1_c"
};

const uint8_t LevelDataLengths[] = {
#include "tiled/include/1-1_s"
    , 0x00
};

__attribute__((always_inline))
uint8_t GetNextMetaTile() {
    if (!hunk_remaining) {
        level_data_index++;
        hunk_remaining = LevelDataLengths[level_data_index];
    }
    const uint8_t tile = LevelData[level_data_index];
    hunk_remaining--;
    return tile;
}

__attribute__((always_inline))
uint8_t GetPrevMetaTile() {
    if (!hunk_remaining) {
        level_data_index--;
        hunk_remaining = LevelDataLengths[level_data_index];
    }
    const uint8_t tile = LevelData[level_data_index];
    hunk_remaining--;
    return tile;
}

uint8_t GetNextWrite(const uint16_t step) {
    if (~step & 1) {
        if (step == 0) {
            attr_column++;
            const uint8_t mask = (attr_column & 1) ? 0x33 : 0xCC;
            for (uint8_t j = 0; j < 8; j++)
                AttributeBuffer[j] &= mask;
        }

        MetatileBuffer[step >> 1] = GetNextMetaTile();

        const uint8_t tile_row  = 2 + step;
        const uint8_t attr_idx  = tile_row >> 2;
        const uint8_t pal       = METATILE_ATTR(MetatileBuffer[step >> 1]);
        const uint8_t is_bottom = (tile_row >> 1) & 1;
        const uint8_t shift     = (attr_column & 1)
                                ? (is_bottom ? 6 : 2)
                                : (is_bottom ? 4 : 0);
        AttributeBuffer[attr_idx] |= (pal << shift);
    }
    return Metatiles[MetatileBuffer[step >> 1] << 2 | (step & 1)];
}

uint8_t GetPrevWrite(const uint16_t step) {
    if (~step & 1) {
        if (step == 0) {
            attr_column++;
            const uint8_t mask = !(attr_column & 1) ? 0x33 : 0xCC;
            for (uint8_t j = 0; j < 8; j++)
                AttributeBuffer[j] &= mask;
        }

        MetatileBuffer[step >> 1] = GetPrevMetaTile();

        const uint8_t tile_row  = 2 + step;
        const uint8_t attr_idx  = tile_row >> 2;
        const uint8_t pal       = METATILE_ATTR(MetatileBuffer[step >> 1]);
        const uint8_t is_bottom = (tile_row >> 1) & 1;
        const uint8_t shift     = (attr_column & 1)
                                ? (is_bottom ? 6 : 2)
                                : (is_bottom ? 4 : 0);
        AttributeBuffer[attr_idx] |= (pal << shift);
    }
    return Metatiles[MetatileBuffer[step >> 1] << 2 | (step & 1)];
}

uint8_t GetCurrentWrite(const uint16_t step) {
    return Metatiles[MetatileBuffer[step >> 1] << 2 | 2 | step & 1];
}