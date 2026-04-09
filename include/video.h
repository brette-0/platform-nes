#ifndef VIDEO_H
#define VIDEO_H

#include <stdint.h>

#ifdef TARGET_NES
enum PPU {
    PPUCTRL     = 0x2000,
    PPUMASK     = 0x2001,
    PPUSTATUS   = 0x2002,
    OAMADDR     = 0x2003,
    OAMDATA     = 0x2004,
    PPUSCROLL   = 0x2005,
    PPUADDR     = 0x2006,
    PPUDATA     = 0x2007,

    OAMDMA      = 0x4014
};

enum MASK {
    BG      = 0x08,
    SPRITE  = 0x10,
};

#else

#endif



void WaitForPresent();
void EnableRendering(uint8_t ppuMask);

#endif