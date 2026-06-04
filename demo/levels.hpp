#ifndef LEVELS_H
#define LEVELS_H
#include <intsh>
using namespace br0::intsh;

constexpr auto levelHeight = 14;

extern const u8 LevelData[];
extern const u8 LevelDataLengths[];
extern const u8 LevelDataAttributes[];
extern u8 hunk_remaining;
extern u16 level_data_index;
extern u8 attr_column;
extern u8 AttributeBuffer[8];
u8 GetPrevWrite(u16 step);
u8 GetNextWrite(u16 step);
u8 GetCurrentNext(u16 step);
u8 GetCurrentPrev(u16 step);
u8 GetPrevMetaTile();
u8 GetNextMetaTile();
#endif