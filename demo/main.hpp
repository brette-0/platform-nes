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

oam_t AdjustSpriteY(std::uint16_t i);
oam_t AdjustSpriteX(std::uint16_t i);

extern std::uint8_t port1;
extern std::uint8_t port2;

extern oam_t playerX;
extern oam_t playerY;

extern int8_t lastDeltaScroll;

extern std::uint16_t levelSize;
extern atomic std::uint16_t xWorldSpace;
extern atomic std::uint16_t lastXWorldSpace;

extern atomic std::uint8_t spriteZeroHandled;

extern sprite_t OAMBuffer[64];

#endif //PLATFORM_NES_MAIN_H