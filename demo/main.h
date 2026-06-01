#ifndef PLATFORM_NES_MAIN_H
#define PLATFORM_NES_MAIN_H
#include "../include/platform-nes/platform-nes.h"

enum spriteZeroStatus {
    S0_DO_CALCULATE,
    S0_DO_CHECK_PERFORM,
    S0_BEING_PERFORMED
};

#define VIEWPORT_MX (VIEWPORT_TX >> 1)
#define VIEWPORT_MY (VIEWPORT_TY >> 1)

uint8_t AdjustSpriteY(uint16_t i);
uint8_t AdjustSpriteX(uint16_t i);

extern uint8_t port1;
extern uint8_t port2;

extern video_t playerX;
extern video_t playerY;

extern int8_t lastDeltaScroll;

extern uint16_t levelSize;
extern atomic uint16_t xWorldSpace;
extern atomic uint16_t lastXWorldSpace;

extern atomic uint8_t spriteZeroHandled;

extern struct sprite_t OAMBuffer[64];

#endif //PLATFORM_NES_MAIN_H