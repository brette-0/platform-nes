#pragma once
#include <platform-nes/platform-nes.hpp>

#include "level/actor.hpp"

enum class spriteZeroStatus : u8 {
    S0_DO_CALCULATE,
    S0_DO_CHECK_PERFORM,
    S0_BEING_PERFORMED
};

/** @brief Viewport width in metatiles (tiles / 2). */
constexpr u16 viewport_mx() { return video::viewport_tx() >> 1; }
/** @brief Viewport height in metatiles (tiles / 2). */
constexpr u16 viewport_my() { return video::viewport_ty() >> 1; }

extern u8 port1;
extern u8 port2;

extern i8 lastDeltaScroll;

extern atomic u16 lastXWorldSpace;

extern atomic u8 spriteZeroHandled;

extern oam::sprite_t OAMBuffer[64];

void SpriteZeroHandler();
extern u8 TileBuffer[56];

extern Actor player;

enum class eLevelStreamCommands : u8 {
    STREAM_LEVEL_LEFT  = 0x00,
    STREAM_LEVEL_RIGHT = 0x01,
    STREAM_LEVEL_DONE  = 0x02,
    STREAM_LEVEL_SWAP  = 0x04,
};

extern atomic enum_flags<eLevelStreamCommands> levelStreamCommand;