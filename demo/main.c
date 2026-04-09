#include "../include/platform-nes.h"
#include "main.h"
#include "tracks.h"

uint8_t port1;
uint8_t port2;
RESET() {
    EnableRendering(0);
    AudioInit();
    TrackPlay(0);
    while (!quit) {
        if (port1 & START) {
#ifndef  TARGET_NES
            quit = 1
#endif

        }

        WaitForPresent();
    }
}

NMI() {
    PollControllers(&port1, &port2);
    AudioUpdate();
}