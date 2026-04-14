#ifndef LEVELS_H
#define LEVELS_H
#include <stdint.h>

extern const uint8_t LevelData[];
extern const uint8_t LevelDataLengths[];
extern const uint8_t LevelDataAttributes[];
extern uint8_t hunk_remaining;
extern uint8_t level_data_index;
uint8_t GetNextWrite(const uint8_t step);
uint8_t GenCurrentWrite(const uint8_t step);
#endif