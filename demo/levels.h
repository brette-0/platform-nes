#ifndef LEVELS_H
#define LEVELS_H
#include <stdint.h>

#define LEVEL_HEIGHT 14

extern const uint8_t LevelData[];
extern const uint8_t LevelDataLengths[];
extern const uint8_t LevelDataAttributes[];
extern uint8_t hunk_remaining;
extern uint16_t level_data_index;
extern uint8_t attr_column;
extern uint8_t AttributeBuffer[8];
uint8_t GetNextWrite(const uint16_t step);
uint8_t GetCurrentWrite(const uint16_t step);
#endif