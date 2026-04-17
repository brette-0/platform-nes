#ifndef HANDLER_H
#define HANDLER_H
#include "platform-nes/technology.h"
#include <stdint.h>

void SpriteZeroHandler(void);
extern atomic uint8_t levelStreamCommand;;
extern uint8_t TileBuffer[56];


enum eLevelStreamCommands {
    STREAM_LEVEL_LEFT  = 0x00,
    STREAM_LEVEL_RIGHT = 0x01,
    STREAM_LEVEL_LATCH = 0x02,
    STREAM_LEVEL_DONE  = 0x04,
};

#endif