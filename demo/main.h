#ifndef PLATFORM_NES_MAIN_H
#define PLATFORM_NES_MAIN_H
#include "../include/platform-nes/platform-nes.h"

enum spriteZeroStatus {
    S0_DO_CALCULATE,
    S0_DO_CHECK_PERFORM,
    S0_BEING_PERFORMED
};

extern uint8_t port1;
extern uint8_t port2;

extern int8_t lastDeltaScroll;

extern uint16_t levelSize;
extern atomic uint16_t xWorldSpace;
extern atomic uint16_t lastXWorldSpace;

extern atomic uint8_t spriteZeroHandled;

// mario's positional information
extern video_t marioXPosCoarse;    // px(x)
extern video_t marioYPosCoarse;    // px(y)
extern uint8_t marioXPosFine;      // spx(x)
extern uint8_t marioYPosFine;      // spx(y)

video_t AdjustSpriteY(uint16_t i);
video_t AdjustSpriteX(uint16_t i);

#define VIEWPORT_MX (VIEWPORT_TX >> 1)
#define VIEWPORT_MY (VIEWPORT_TY >> 1)

#endif //PLATFORM_NES_MAIN_H