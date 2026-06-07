#ifndef LEVELS_H
#define LEVELS_H

#include "types.hpp"

#include <intsh>
#include <platform-nes/technology.hpp>
using namespace br0::intsh;

namespace demo::level {
    constexpr auto levelHeight = 14;
    extern u16 nColumns;
    extern const u8* TileData;
    extern const u8* HunkLengths;
    extern const u8 LevelDataAttributes[];
    extern u8 hunk_remaining;
    extern u16 level_data_index;
    extern u8 attr_column;
    extern u8 AttributeBuffer[8];
    extern const Level Levels[];
    u8 GetPrevWrite(u16 step);
    u8 GetNextWrite(u16 step);
    u8 GetCurrentNext(u16 step);
    u8 GetCurrentPrev(u16 step);
    u8 GetPrevMetaTile();
    u8 GetNextMetaTile();
    MINSIZE bool BuildLevelSize();
    bool LoadLevel(u16 n);
}
#endif
