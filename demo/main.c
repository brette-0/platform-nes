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

uint16_t levelSize;
uint16_t xWorldSpace;

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
    WriteBufferToVideoMemory(VIEWPORT_X - sizeof(msg_mario), 0, SIZED_OBJ(msg_mario), 0);

    for (uint8_t i = 0; i < 2 + VIEWPORT_X; i += 2) {
        WriteProviderToVideoMemory(
            i, 2,
            GetNextWrite, 28, 1
        );

        WriteProviderToVideoMemory(
            i + 1, 2,
            GetCurrentWrite, 28, 1
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

        WaitForPresent();
    }
}

NMI {
    RefreshSprites();
    PollControllers(&port1, &port2);

    const int8_t deltaScroll = !!(port1 & LEFT) * -1 + !!(port1 & RIGHT) * 1; // NOLINT(*-narrowing-conversions)

    if (deltaScroll) {
        xWorldSpace = deltaScroll > 0
            ? xWorldSpace + VIEWPORT_X + deltaScroll < levelSize
                ? levelSize - VIEWPORT_X
                : xWorldSpace + deltaScroll
            :  xWorldSpace + deltaScroll > xWorldSpace
                ? 0
                : xWorldSpace + deltaScroll;
    }

    AudioUpdate();
    SetNextIRQHandler(IRQ_SPRITE_ZERO);
    SetScroll(0, 0);
}

IRQ(SPRITE_ZERO) {
    SetScroll(xWorldSpace, 0);
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

    for (uint8_t i = 0; i < 255; i++) {
        if (LevelDataLengths[i] == 0)
            return;

        temp += LevelDataLengths[i];

        while (temp > LEVEL_HEIGHT) {
            levelSize++;
            temp -= LEVEL_HEIGHT;
        }
    }

    // TODO: implement universal 'Error' out
}