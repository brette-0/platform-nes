#ifndef LEVELS_H
#define LEVELS_H
#include <cstdint>

#define LEVEL_HEIGHT 14

extern const std::uint8_t LevelData[];
extern const std::uint8_t LevelDataLengths[];
extern const std::uint8_t LevelDataAttributes[];
extern std::uint8_t hunk_remaining;
extern std::uint16_t level_data_index;
extern std::uint8_t attr_column;
extern std::uint8_t AttributeBuffer[8];
std::uint8_t GetPrevWrite(std::uint16_t step);
std::uint8_t GetNextWrite(std::uint16_t step);
std::uint8_t GetCurrentNext(std::uint16_t step);
std::uint8_t GetCurrentPrev(std::uint16_t step);
std::uint8_t GetPrevMetaTile();
std::uint8_t GetNextMetaTile();
#endif