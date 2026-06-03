#ifndef HANDLER_H
#define HANDLER_H
#include <platform-nes/technology.hpp>
#include <intsh>
using namespace br0::intsh;

void SpriteZeroHandler();
extern atomic u8 levelStreamCommand;;
extern u8 TileBuffer[56];


enum eLevelStreamCommands {
    STREAM_LEVEL_LEFT  = 0x00,
    STREAM_LEVEL_RIGHT = 0x01,
    STREAM_LEVEL_DONE  = 0x02,
    STREAM_LEVEL_SWAP  = 0x04,
};

#endif