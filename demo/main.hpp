#ifndef PLATFORM_NES_MAIN_H
#define PLATFORM_NES_MAIN_H
#include <platform-nes/platform-nes.hpp>

enum spriteZeroStatus {
    S0_DO_CALCULATE,
    S0_DO_CHECK_PERFORM,
    S0_BEING_PERFORMED
};

/** @brief Viewport width in metatiles (tiles / 2). */
constexpr u16 viewport_mx() { return video::viewport_tx() >> 1; }
/** @brief Viewport height in metatiles (tiles / 2). */
constexpr u16 viewport_my() { return video::viewport_ty() >> 1; }

oam::oam_t AdjustSpriteY(u16 i);
oam::oam_t AdjustSpriteX(u16 i);

extern u8 port1;
extern u8 port2;

extern oam::oam_t playerX;
extern oam::oam_t playerY;

extern i8 lastDeltaScroll;

extern u16 levelSize;
extern atomic u16 xWorldSpace;
extern atomic u16 lastXWorldSpace;

extern atomic u8 spriteZeroHandled;

extern oam::sprite_t OAMBuffer[64];

#endif //PLATFORM_NES_MAIN_H