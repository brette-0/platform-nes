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

struct sprite_t OAMBuffer[64];

static uint8_t Clear(uint16_t _);
static uint8_t AdjustSpriteY(uint16_t i);
static uint8_t AdjustSpriteX(uint16_t i);

RESET() {
    FlushVideoRAM(0x24, 0x00);

    PopulateFromProvider(
        (uint8_t*)&oamBuffer,
        SPRITE_SLOT(0) + offsetof(struct sprite_t, y),
        Clear, 8, SPRITE_STRIDE
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

NMI() {
    RefreshSprites();
    PollControllers(&port1, &port2);
    AudioUpdate();
}

IRQ(SPRITE_ZERO) {
    SetScroll(1, 0);
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