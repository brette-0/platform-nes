#include "levels.h"
#include "metatiles.h"

#include <stdint.h>

uint8_t hunk_remaining;
uint8_t level_data_index;

uint8_t MetatileBuffer[14];
uint8_t AttributeBuffer[7];

const uint8_t LevelData[] = {
#include "tiled/include/1-1_c"
};

const uint8_t LevelDataLengths[] = {
#include "tiled/include/1-1_s"
};

const uint8_t LevelDataAttributes[] = {};

__attribute__((always_inline))
uint8_t GetNextMetaTile() {
    const uint8_t tile = LevelData[level_data_index];       \
    if (!hunk_remaining) {                                  \
        level_data_index++;                                 \
        hunk_remaining = LevelDataLengths[level_data_index];\
    } hunk_remaining--;

    return tile;
}

uint8_t GetNextWrite(const uint8_t step) {
    if (step & 1) {
        MetatileBuffer[step >> 1] = GetNextMetaTile();
    }
    return Metatiles[MetatileBuffer[step >> 1] << 2 | step & 1];
}

uint8_t GetCurrentWrite(const uint8_t step) {
    return Metatiles[MetatileBuffer[step >> 1] << 2 | 2 | step & 1];
}