#include <platform-nes/video.h>
#include <platform-nes/technology.h>
#include <platform-nes/shadow.h>
#include <stdint.h>

const uint16_t PatternTables    = 0;
const uint16_t NameTables       = 0x2000;
const uint16_t PaletteTables    = 0x3f00;
const uint16_t nVideoRam        = 0x1000;

inline static uint16_t xy_to_nt_addr(uint16_t x, uint16_t y) {
    uint16_t base = 0x2000;
    uint16_t nt_h = ((x >> 8) & 1) << 10;   // +$0400 if NT1/NT3
    uint16_t nt_v = (y / 30) << 11;          // +$0800 if NT2/NT3
    uint16_t col  = x & 0x1F;                // 0-31 tile column
    uint16_t row  = (y % 30);                // 0-29 tile row within NT

    return base + nt_h + nt_v + row * 32 + col;
}

void WaitForPresent() {
    while (1) {}
}

void EnableRendering(uint8_t ppuMask) {
    *(volatile uint8_t*)PPUCTRL = 0x80;
    *(volatile uint8_t*)PPUMASK = ppuMask;
}

void FlushVideoRAM(const uint8_t byte) {
    PEEK(PPUSTATUS);
    POKE(PPUADDR, NameTables >> 8);
    POKE(PPUADDR, NameTables & 0xFF);

    for (uint16_t i = 0; i < nVideoRam; i++) {
        POKE(PPUDATA, byte);
    }
}

void SetScroll(uint16_t x, uint16_t y) {
    xScroll = x; yScroll = y;


    // this code is shit needs fixing
    if (y >= 240) {
        y -= 240;
        y ^= 0x100;
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

void WriteBufferToVideoMemory(
    const uint16_t x, const uint16_t y, const uint8_t* source, const uint8_t sBuffer, uint8_t polarity
) {
    uint16_t offset = xy_to_nt_addr(x, y);
    PEEK(PPUSTATUS);
    POKE(PPUADDR, (uint8_t)(offset >> 8));
    POKE(PPUADDR, (uint8_t)(offset & 0xFF));

    for (uint8_t i = 0; i < sBuffer; i++) {
        POKE(PPUDATA, source[i]);
    }
}