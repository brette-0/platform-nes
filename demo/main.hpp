#ifndef PLATFORM_NES_MAIN_H
#define PLATFORM_NES_MAIN_H
#include <platform-nes/platform-nes.hpp>

enum spriteZeroStatus {
    S0_DO_CALCULATE,
    S0_DO_CHECK_PERFORM,
    S0_BEING_PERFORMED
};

#define VIEWPORT_MX (VIEWPORT_TX >> 1)
#define VIEWPORT_MY (VIEWPORT_TY >> 1)

oam_t AdjustSpriteY(u16 i);
oam_t AdjustSpriteX(u16 i);

extern u8 port1;
extern u8 port2;

extern oam_t playerX;
extern oam_t playerY;

extern i8 lastDeltaScroll;

extern u16 levelSize;
extern atomic u16 xWorldSpace;
extern atomic u16 lastXWorldSpace;

extern atomic u8 spriteZeroHandled;

extern sprite_t OAMBuffer[64];

#endif //PLATFORM_NES_MAIN_H