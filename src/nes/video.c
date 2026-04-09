#include "../include/video.h"
#include <stdint.h>

void WaitForPresent() {
    while (1) {}
}

void EnableRendering(uint8_t ppuMask) {
    *(volatile uint8_t*)PPUCTRL = 0x80;
    *(volatile uint8_t*)PPUMASK = ppuMask;
}