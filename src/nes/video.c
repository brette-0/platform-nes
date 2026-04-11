#include <platform-nes/video.h>
#include <platform-nes/technology.h>
#include <platform-nes/shadow.h>
#include <stdint.h>

void WaitForPresent() {
    while (1) {}
}

void EnableRendering(uint8_t ppuMask) {
    *(volatile uint8_t*)PPUCTRL = 0x80;
    *(volatile uint8_t*)PPUMASK = ppuMask;
}

void FlushVideoRAM(const uint8_t byte) {
    PEEK(PPUSTATUS);
    POKE(PPUADDR, 0);
    POKE(PPUADDR, 0);

    for (uint16_t i = 0; i < 0x2000; i++) {
        POKE(PPUADDR, byte);
    }
}

void SetScroll(uint16_t x, uint16_t y) {
    if (y >= 240) {
        y -= 240;
        y ^= 0x100; /* flip nametable bit */
    }

    if (y >= 240) {
        y -= 240;
        y ^= 0x100;
    }

    uint8_t nt = x >> 8 & 0x01
               | y >> 7 & 0x02;

    POKE(PPUSCROLL, SPPUCRTL & 0xFC | nt);

    POKE(PPUSCROLL, (uint8_t)(x & 0xFF));
    POKE(PPUSCROLL, (uint8_t)(y & 0xFF));
}

void WriteBufferToVideoMemory(uint16_t target, const uint8_t* source, uint8_t sBuffer, uint8_t polarity) {
    PEEK(PPUSTATUS);
    POKE(PPUADDR, (uint8_t)(target & 0xFF));
    POKE(PPUADDR, (uint8_t)(target & 0xFF));

    for (uint8_t i = 0; i < sBuffer; i++) {
        POKE(PPUDATA, source[i]);
    }
}