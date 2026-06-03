#include "levels.hpp"
#include "metatiles.hpp"

#include <cstdint>

std::uint8_t hunk_remaining;
std::uint16_t level_data_index;

std::uint8_t MetatileBuffer[14];
std::uint8_t AttributeBuffer[8];
std::uint8_t attr_column = 0xFF;

const std::uint8_t LevelData[] = {
#include "tiled/include/1-1_c"
};

const std::uint8_t LevelDataLengths[] = {
#include "tiled/include/1-1_s"
    , 0x00
};

__attribute__((always_inline))
std::uint8_t GetNextMetaTile() {
    if (!hunk_remaining) {
        level_data_index++;
        hunk_remaining = LevelDataLengths[level_data_index];
    }

    hunk_remaining--;
    return LevelData[level_data_index];
}

__attribute__((always_inline))
std::uint8_t GetPrevMetaTile() {
    if (!hunk_remaining) {
        level_data_index--;
        hunk_remaining = LevelDataLengths[level_data_index];
    }

    hunk_remaining--;
    return LevelData[level_data_index];
}

std::uint8_t GetNextWrite(const std::uint16_t step) {
    if (~step & 1) {
        if (step == 0) {
            attr_column++;
            const std::uint8_t mask = attr_column & 1 ? 0x33 : 0x00;
            for (auto & j : AttributeBuffer)
                j &= mask;
        }

        MetatileBuffer[step >> 1] = GetNextMetaTile();

        const std::uint8_t tile_row  = 2 + step;
        const std::uint8_t attr_idx  = tile_row >> 2;
        const std::uint8_t pal       = METATILE_ATTR(MetatileBuffer[step >> 1]);
        const std::uint8_t is_bottom = tile_row >> 1 & 1;
        const std::uint8_t shift     = attr_column & 1
                                ? (is_bottom ? 6 : 2)
                                : is_bottom ? 4 : 0;
        AttributeBuffer[attr_idx] |= pal << shift;
    }
    return Metatiles[MetatileBuffer[step >> 1] << 2 | step & 1];
}

std::uint8_t GetPrevWrite(const std::uint16_t step) {
    if (~step & 1) {
        if (step == 0) {
            attr_column++;
            const std::uint8_t mask = attr_column & 1 ? 0xCC : 0x00;
            for (auto & j : AttributeBuffer)
                j &= mask;
        }

        MetatileBuffer[step >> 1] = GetPrevMetaTile();

        const std::uint8_t tile_row  = 29 - step;
        const std::uint8_t attr_idx  = tile_row >> 2;
        const std::uint8_t pal       = METATILE_ATTR(MetatileBuffer[step >> 1]);
        const std::uint8_t is_bottom = tile_row >> 1 & 1;
        const std::uint8_t shift     = attr_column & 1
                                ? (is_bottom ? 4 : 0)
                                : is_bottom ? 6 : 2;
        AttributeBuffer[attr_idx] |= pal << shift;
    }
    return Metatiles[MetatileBuffer[step >> 1] << 2 | ~step & 1];
}

std::uint8_t GetCurrentNext(const std::uint16_t step) {
    return Metatiles[MetatileBuffer[step >> 1] << 2 | 2 | step & 1];
}

std::uint8_t GetCurrentPrev(const std::uint16_t step) {
    return Metatiles[MetatileBuffer[step >> 1] << 2 | 2 | ~step & 1];
}