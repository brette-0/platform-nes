#pragma once
#include <platform-nes/platform-nes.hpp>

#include "level/player.hpp"

namespace level {
    extern bool paused;

    /** @brief Viewport width in metatiles (tiles / 2). */
    constexpr u16 viewport_mx() { return video::viewport_tx() >> 1; }
    /** @brief Viewport height in metatiles (tiles / 2). */
    constexpr u16 viewport_my() { return video::viewport_ty() >> 1; }

    enum class eLevelStreamCommands : u8 {
        STREAM_LEVEL_LEFT  = 0x00,
        STREAM_LEVEL_RIGHT = 0x01,
        STREAM_LEVEL_DONE  = 0x02,
        STREAM_LEVEL_SWAP  = 0x04,
    };

    extern u8 port1;
    extern u8 port2;

    extern u8 lastPort1;
    extern u8 lastPort2;

    extern u16 cameraX;

    extern i8 lastDeltaScroll;

    extern atomic u16 lastXWorldSpace;

    extern oam::sprite_t OAMBuffer[64];

    extern u8 TileBuffer[56];

    extern Player player1;
#ifdef PLAYER2_SUPPORTED
    extern Player player2;
#endif

    extern atomic tech::enum_flags<eLevelStreamCommands> levelStreamCommand;

    // Deferred VRAM write queue (coin pickups -> NMI drain).
    // Pushing is done by the player logic; draining is done by the NMI.
    void CoinVramPush(u16 address, u8 value);

    // Runs the level-playing mode: bank setup, level load, and the gameplay
    // loop. Called from the top-level mode dispatch while gameMode == Level.
    void main();

    // Called (as an ordinary C++ function, not a raw vector jump) from
    // main.cpp's nmiTrampoline/irqTrampoline whenever gameMode == Level.
    void nmi_handler();
    void irq_handler();
}
