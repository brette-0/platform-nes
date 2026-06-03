#include <platform-nes>
#include "main.hpp"
#include "graphics.hpp"
#include "levels.hpp"
#include "metasprites.hpp"
#include "handlers.hpp"
#include "colors.hpp"

std::uint8_t port1;
std::uint8_t port2;

oam_t playerX;
oam_t playerY;

int8_t lastDeltaScroll;

std::uint16_t levelSize;
atomic std::uint16_t xWorldSpace;
atomic std::uint16_t lastXWorldSpace;

atomic std::uint8_t spriteZeroHandled;

sprite_t OAMBuffer[64] __attribute__((aligned(256)));

static oam_t Clear(std::uint16_t _);
static bool    BuildLevelSize();

RESET {
    if (!BuildLevelSize()) {
        reset();    // spin reset on NES, exit on SDL3
    }
    FlushVideoRAM(0x24, 0x00);

    PopulateOAMFromProvider(OAMBuffer, 0, y, Clear, 64);

    // fill in with mario metatiles
    PopulateOAMFromBuffer(OAMBuffer, 1, tile, msMario, 8);
    PopulateOAMFromProvider(OAMBuffer, 1, y, AdjustSpriteY, 8);
    PopulateOAMFromProvider(OAMBuffer, 1, x, AdjustSpriteX, 8);

    WriteBufferToPaletteMemory(BG_0,         SIZED_OBJ(BGColours));
    WriteBufferToPaletteMemory(SPRITE_0 + 1, SIZED_OBJ(marioColors));
    WriteBufferToVideoMemory(VIEWPORT_TX - sizeof(msg_mario), 0, SIZED_OBJ(msg_mario), 0);

    WriteSingleToVideoMemory(0, 1, 0x2e);
    OAMBuffer[0] = ( sprite_t){
        .y = 8,
        .tile = 0xff,
        .attributes = 0,
        .x = 0
    };

    hunk_remaining = LevelDataLengths[0];
    for (auto i = 0; i < 2 + VIEWPORT_TX; i += 2) {
        WriteProviderToVideoMemory(
            i, 2,
            GetNextWrite, 28, 1
        );

        WriteProviderToVideoMemory(
            i + 1, 2,
            GetCurrentNext, 28, 1
        );

        WriteBufferToAttributeMemory(i & ~3, 2, AttributeBuffer, 8, 1);
    }

    SetScroll(0, 0);

    AudioInit();
    TrackPlay(0);
    RefreshSprites(OAMBuffer);   /* seed the first frame's sprite snapshot */
    EnableRendering(BG_ADDR, BG_L | SPRITE_L);
    // ReSharper disable once CppDFAEndlessLoop
    while (!quit) {
        if (port1 & START) {
#ifndef  TARGET_NES
            quit = 1;
#endif

        }

        WaitThenReactToSpriteZero(0, 16, SpriteZeroHandler, &spriteZeroHandled);

        WaitForPresent();
    }
}

NMI {
    SetColorPriority(BLUE);
    RefreshSprites(OAMBuffer);

    spriteZeroHandled = 0;

    if (levelStreamCommand & STREAM_LEVEL_DONE) VRAM {
        if (levelStreamCommand & STREAM_LEVEL_RIGHT) {
            WriteBufferToVideoMemory((lastXWorldSpace >> 3) + VIEWPORT_TX + 0, 2, TileBuffer, 28, 1);
            WriteBufferToVideoMemory((lastXWorldSpace >> 3) + VIEWPORT_TX + 1, 2, TileBuffer + 28, 28, 1);
            if (!(levelStreamCommand & STREAM_LEVEL_SWAP))
                WriteBufferToAttributeMemory((lastXWorldSpace >> 3) + VIEWPORT_TX & ~3, 2, AttributeBuffer, 8, 1);
        } else {
            WriteBufferToVideoMemory((lastXWorldSpace >> 3) - 1, 2, TileBuffer, 28, 1);
            WriteBufferToVideoMemory((lastXWorldSpace >> 3) - 2, 2, TileBuffer + 28, 28, 1);
            if (!(levelStreamCommand & STREAM_LEVEL_SWAP))
                WriteBufferToAttributeMemory((lastXWorldSpace >> 3) - 2 & ~3, 2, AttributeBuffer, 8, 1);
        }
    }

    SetScroll(0, 0);
    if (levelStreamCommand & STREAM_LEVEL_DONE) {
        levelStreamCommand = 0;
    }

    SetColorPriority(0);
}

static oam_t Clear(const std::uint16_t _) {
    return 0xef;
}

oam_t AdjustSpriteY(const std::uint16_t i) {
    return playerY + (i >> 1) * 8;
}

oam_t AdjustSpriteX(const std::uint16_t i) {
    return playerX + (i & 1) * 8;
}

MINSIZE static bool BuildLevelSize() {
    std::uint8_t temp = 0;
    levelSize    = 0;

    for (std::uint16_t i = 0; i < 0xffff; i++) {
        if (LevelDataLengths[i] == 0)
            return true;

        temp += LevelDataLengths[i];

        while (temp >= LEVEL_HEIGHT) {
            levelSize++;
            temp -= LEVEL_HEIGHT;
        }
    }

    return false;
}