#ifndef HANDLER_H
#define HANDLER_H
#include <stdint.h>
void SpriteZeroHandler(void);

extern uint8_t TileBuffer[28];

enum LevelFetchAction {
    prev    = 0,
    next    = 1,
    latched = 2,
};

#endif