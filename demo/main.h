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

#endif //PLATFORM_NES_MAIN_H