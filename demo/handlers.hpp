#ifndef HANDLER_H
#define HANDLER_H
#include <platform-nes/technology.hpp>
#include <cstdint>

void SpriteZeroHandler();
extern atomic uint8_t levelStreamCommand;;
extern std::uint8_t TileBuffer[56];


enum eLevelStreamCommands {
    STREAM_LEVEL_LEFT  = 0x00,
    STREAM_LEVEL_RIGHT = 0x01,
    STREAM_LEVEL_DONE  = 0x02,
    STREAM_LEVEL_SWAP  = 0x04,
};

#endif