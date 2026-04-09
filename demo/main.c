#include "../include/platform-nes.h"
#include "main.h"

uint8_t game_exit = 0;

uint8_t port1;
uint8_t port2;

RESET() {
    while (!quit) {
        if (port1 & START) {
            game_exit = 1;
        }

        WaitForPresent();
    }
}

NMI() {
    PollControllers(&port1, &port2);
}