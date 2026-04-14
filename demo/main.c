#include <platform-nes/platform-nes.h>
#include "main.h"
#include "graphics.h"
#include "levels.h"

uint8_t port1;
uint8_t port2;

RESET() {
    FlushVideoRAM(0x24, 0x00);

    WriteBufferToPaletteMemory(0, SIZED_OBJ(BGColours));

    for (uint8_t i = 0; i < VIEWPORT_X; i += 2) {
        WriteProviderToVideoMemory(
            i, 2,
            GetNextWrite, 28, 1
        );

        WriteProviderToVideoMemory(
            i + 1, 2,
            GetCurrentWrite, 28, 1
        );
    }

    SetScroll(0, 0);

    AudioInit();
    TrackPlay(0);
    EnableRendering(BG_ADDR, BG_L);
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
    PollControllers(&port1, &port2);
    AudioUpdate();
}