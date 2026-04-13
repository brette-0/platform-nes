#include <platform-nes/platform-nes.h>
#include "main.h"
#include "graphics.h"

uint8_t port1;
uint8_t port2;
RESET() {
    FlushVideoRAM(0);

    WriteBufferToPaletteMemory(0, SIZED_OBJ(BGColours));

    WriteBufferToVideoMemory(
        (VIEWPORT_X - sizeof(msg_hi)) >> 1,
        VIEWPORT_Y >> 1,
        SIZED_OBJ(msg_hi),
        0
    );

    SetScroll(0, 0);

    AudioInit();
    TrackPlay(0);
    EnableRendering(BG_L);
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