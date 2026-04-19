#include <platform-nes/platform-nes.h>
#include <stddef.h>
#include "main.h"
#include "graphics.h"
#include "levels.h"
#include "metasprites.h"
#include "handlers.h"

#define SPRITE_STRIDE  sizeof(struct sprite_t)
#define SPRITE_SLOT(i) ((i) * SPRITE_STRIDE)

uint8_t port1;
uint8_t port2;

uint8_t xPlayer;    // relative to top left of player
uint8_t yPlayer;
int8_t lastDeltaScroll;

uint16_t levelSize;
atomic uint16_t xWorldSpace;
atomic uint16_t lastXWorldSpace;

atomic uint8_t spriteZeroHandled;

struct sprite_t OAMBuffer[64];

static uint8_t Clear(uint16_t _);
static uint8_t AdjustSpriteY(uint16_t i);
static uint8_t AdjustSpriteX(uint16_t i);
static void    BuildLevelSize();

RESET {
    BuildLevelSize();
    FlushVideoRAM(0x24, 0x00);

    PopulateFromProvider(
        (uint8_t*)&oamBuffer,
        SPRITE_SLOT(0) + offsetof(struct sprite_t, y),
        Clear, 64, SPRITE_STRIDE
    );


    // fill in with mario metatiles
    PopulateFromBuffer(
        (uint8_t*)&oamBuffer, SPRITE_SLOT(1),
        (const uint8_t*)msMario, 8 * SPRITE_STRIDE, 1
    );

    PopulateFromProvider(
        (uint8_t*)&oamBuffer,
        SPRITE_SLOT(1) + offsetof(struct sprite_t, y),
        AdjustSpriteY, 8, SPRITE_STRIDE
    );

    PopulateFromProvider(
        (uint8_t*)&oamBuffer,
        SPRITE_SLOT(1) + offsetof(struct sprite_t, x),
        AdjustSpriteX, 8, SPRITE_STRIDE
    );

    WriteBufferToPaletteMemory(0, SIZED_OBJ(BGColours));
    WriteBufferToVideoMemory(VIEWPORT_TX - sizeof(msg_mario), 0, SIZED_OBJ(msg_mario), 0);

    WriteSingleToVideoMemory(0, 1, 0x2e);
    OAM_BUFFER[0] = (struct sprite_t){
        .y = 8,
        .tile = 0xff,
        .attributes = 0,
        .x = 0
    };

    hunk_remaining = LevelDataLengths[0];
    for (uint8_t i = 0; i < 2 + VIEWPORT_TX; i += 2) {
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
    PollControllers(&port1, &port2);
    RefreshSprites();

    const int8_t deltaScroll = !!(port1 & LEFT) * -1 + !!(port1 & RIGHT) * 1; // NOLINT(*-narrowing-conversions)

    if (deltaScroll) {
        xWorldSpace = deltaScroll > 0
            ? xWorldSpace + deltaScroll > (uint16_t)((levelSize - VIEWPORT_MX) << 4)
                ? (uint16_t)((levelSize - VIEWPORT_MX) << 4)
                : xWorldSpace + deltaScroll
            : xWorldSpace + deltaScroll > xWorldSpace
                ? 0
                : xWorldSpace + deltaScroll;

        if (!levelStreamCommand && !(xWorldSpace & 0x0f)) {
            if (deltaScroll > 0) {
                if (xWorldSpace > lastXWorldSpace && xWorldSpace != (levelSize - VIEWPORT_MX) << 4) {
                    levelStreamCommand =    STREAM_LEVEL_LATCH |
                                            STREAM_LEVEL_RIGHT | (
                                                lastDeltaScroll < 0
                                                    ? STREAM_LEVEL_SWAP
                                                    : 0
                                                );
                    lastDeltaScroll = deltaScroll;
                    lastXWorldSpace = xWorldSpace;

                }
            } else if (xWorldSpace >= 0x10) {
                levelStreamCommand =    STREAM_LEVEL_LATCH |
                                        STREAM_LEVEL_LEFT  | (
                                            lastDeltaScroll > 0
                                                ? STREAM_LEVEL_SWAP
                                                : 0
                                            );
                lastDeltaScroll = deltaScroll;
                lastXWorldSpace = xWorldSpace;
            }
        }
    }

    spriteZeroHandled = 0;

    if (levelStreamCommand & STREAM_LEVEL_DONE) VRAM {
        if (levelStreamCommand & STREAM_LEVEL_RIGHT) {
            WriteBufferToVideoMemory(((lastXWorldSpace) >> 3) + VIEWPORT_TX + 0, 2, TileBuffer, 28, 1);
            WriteBufferToVideoMemory(((lastXWorldSpace) >> 3) + VIEWPORT_TX + 1, 2, TileBuffer + 28, 28, 1);
        } else {
            WriteBufferToVideoMemory(((lastXWorldSpace) >> 3) - 1, 2, TileBuffer, 28, 1);
            WriteBufferToVideoMemory(((lastXWorldSpace) >> 3) - 2, 2, TileBuffer + 28, 28, 1);
        }
    }

    SetScroll(0, 0);
    if (levelStreamCommand & STREAM_LEVEL_DONE) {
        levelStreamCommand = 0;
    }

    SetColorPriority(0);
}

static uint8_t Clear(uint16_t _) {
    return 0xef;
}

static uint8_t AdjustSpriteY(uint16_t i) {
    return yPlayer + (i >> 1) * 8;
}

static uint8_t AdjustSpriteX(uint16_t i) {
    return xPlayer + (i & 1) * 8;
}

__attribute__((minsize))
static void BuildLevelSize() {
    uint8_t temp = 0;
    levelSize    = 0;

    for (uint16_t i = 0; i < 0xffff; i++) {
        if (LevelDataLengths[i] == 0)
            return;

        temp += LevelDataLengths[i];

        while (temp >= LEVEL_HEIGHT) {
            levelSize++;
            temp -= LEVEL_HEIGHT;
        }
    }

    // TODO: implement universal 'Error' out
}