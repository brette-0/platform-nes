#include "levels.h"

#include <stdint.h>

extern uint8_t hunk_remaining;
extern uint8_t level_data_index;

const uint8_t LevelData[] = {
#include "tiled/include/1-1_c"
};

const uint8_t LevelDataLengths[] = {
#include "tiled/include/1-1_s"
};

inline uint8_t GetNextTile() {
    const uint8_t tile = LevelData[level_data_index];
    if (!hunk_remaining) {
        level_data_index++;
        hunk_remaining = LevelDataLengths[level_data_index];
    } hunk_remaining--;

    return tile;
}