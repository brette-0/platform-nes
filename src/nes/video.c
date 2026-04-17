#include <platform-nes/video.h>
#include <platform-nes/technology.h>
#include <stdint.h>

#include <stdbool.h>

const uint16_t PatternTables    = 0;
const uint16_t NameTables       = 0x2000;
const uint16_t PaletteTables    = 0x3f00;
const uint16_t nVideoRam        = 0x800;
uint16_t xScroll = 0;
uint16_t yScroll = 0;
uint8_t SPPUCTRL;
uint8_t SPPUMASK;

inline static uint16_t xy_to_nt_addr(uint16_t x, uint16_t y) {
    uint16_t base = 0x2000;
    uint16_t nt_h = ((x >> 5) & 1) << 10;
    uint16_t nt_v = (y / 30) << 11;
    uint16_t col  = x & 0x1F;
    uint16_t row  = (y % 30);

    return base + nt_h + nt_v + row * 32 + col;
}

inline static uint16_t xy_to_at_addr(uint16_t x, uint16_t y) {
    uint16_t base = 0x2000;
    uint16_t nt_h = ((x >> 5) & 1) << 10;
    uint16_t nt_v = (y / 30) << 11;
    uint16_t col  = x & 0x1F;
    uint16_t row  = (y % 30);

    return base + nt_h + nt_v + 0x3C0 + (row / 4) * 8 + (col / 4);
}

// TODO: This is a bad name, it does not actually wait on NES and shouldn't for NES multithreading
//       a true 'wait for present' on NES would infinite loop, but won't return to main thread
//       without special return tech which we don't have yet
void WaitForPresent() {

}

void EnableRendering(uint8_t ppuCtrl_, uint8_t ppuMask_) {
    SPPUMASK = ppuMask_;
    SPPUCTRL = 0x80 | ppuCtrl_;
    POKE(PPUCTRL, SPPUCTRL);
    POKE(PPUMASK, SPPUMASK);
}

void FlushVideoRAM(const uint8_t nt, const uint8_t at) {
    PEEK(PPUSTATUS);
    POKE(PPUADDR, NameTables >> 8);
    POKE(PPUADDR, NameTables & 0xFF);

    for (uint8_t page = 0; page < nVideoRam / 0x400; page++) {
        for (uint8_t nt_hunk = 0; nt_hunk < 0xf0; nt_hunk++) {
            POKE(PPUDATA, nt);
            POKE(PPUDATA, nt);
            POKE(PPUDATA, nt);
            POKE(PPUDATA, nt);
        }
        for (uint8_t at_hunk = 0; at_hunk < 0x10; at_hunk++) {
            POKE(PPUDATA, at);
            POKE(PPUDATA, at);
            POKE(PPUDATA, at);
            POKE(PPUDATA, at);

        }
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

    SPPUCTRL = (SPPUCTRL & 0xFC) | nt;
    POKE(PPUCTRL, SPPUCTRL);

    POKE(PPUSCROLL, (uint8_t)(x & 0xFF));
    POKE(PPUSCROLL, (uint8_t)(y & 0xFF));
}

void DeltaScroll(int8_t x, int8_t y) {
    SetScroll((uint16_t)(xScroll + x), (uint16_t)(yScroll + y));
}

void WriteBufferToVideoMemory(
    const uint16_t x, const uint16_t y, const uint8_t* source, const uint8_t sBuffer, uint8_t polarity
) {
    const uint16_t offset = xy_to_nt_addr(x, y);
    PEEK(PPUSTATUS);
    POKE(PPUADDR, (uint8_t)(offset >> 8));
    POKE(PPUADDR, (uint8_t)(offset & 0xFF));

    for (uint8_t i = 0; i < sBuffer; i++) {
        POKE(PPUDATA, source[i]);
    }
}

void WriteSingleToVideoMemory(const uint16_t x, const uint16_t y, uint8_t value) {
    const uint16_t offset = xy_to_nt_addr(x, y);
    PEEK(PPUSTATUS);
    POKE(PPUADDR, (uint8_t)(offset >> 8));
    POKE(PPUADDR, (uint8_t)(offset & 0xFF));
    POKE(PPUDATA, value);
}

void WriteBufferToPaletteMemory(const uint8_t offset, const uint8_t* source, const uint8_t sBuffer) {
    PEEK(PPUSTATUS);
    POKE(PPUADDR, (uint8_t)((offset + PaletteTables) >> 8));
    POKE(PPUADDR, (uint8_t)( offset + PaletteTables  &  0xFF));

    for (uint8_t i = 0; i < sBuffer; i++) {
        POKE(PPUDATA, source[i]);
    }
}

void WriteSingleToPaletteMemory(const uint8_t offset, uint8_t value) {
    PEEK(PPUSTATUS);
    POKE(PPUADDR, (uint8_t)((offset + PaletteTables) >> 8));
    POKE(PPUADDR, (uint8_t)( offset + PaletteTables  &  0xFF));
    POKE(PPUDATA, value);
}

void WriteProviderToVideoMemory(
    const uint16_t x, const uint16_t y, uint8_t (*fn)(uint8_t), const uint8_t amt, const uint8_t polarity
) {
    const uint16_t offset = xy_to_nt_addr(x, y);
    SPPUCTRL &= ~POLARITY;
    if (polarity) SPPUCTRL |= POLARITY;

    POKE(PPUCTRL, SPPUCTRL);

    PEEK(PPUSTATUS);
    POKE(PPUADDR, (uint8_t)(offset >> 8));
    POKE(PPUADDR, (uint8_t)(offset & 0xFF));

    for (uint8_t i = 0; i < amt; i++) {
        POKE(PPUDATA, fn(i));
    }
}

void WriteBufferToAttributeMemory(
    const uint16_t x, const uint16_t y, const uint8_t* source, const uint8_t sBuffer, const uint8_t polarity
) {
    const uint16_t offset = xy_to_at_addr(x, y);

    SPPUCTRL &= ~POLARITY;
    POKE(PPUCTRL, SPPUCTRL);

    PEEK(PPUSTATUS);
    POKE(PPUADDR, (uint8_t)(offset >> 8));
    POKE(PPUADDR, (uint8_t)(offset & 0xFF));

    for (uint8_t i = 0; i < sBuffer; i++) {
        POKE(PPUDATA, source[i]);
        if (polarity) {
            PEEK(PPUDATA);
            PEEK(PPUDATA);
            PEEK(PPUDATA);
            PEEK(PPUDATA);
            PEEK(PPUDATA);
            PEEK(PPUDATA);
            PEEK(PPUDATA);
        }
    }
}

void WriteSingleToAttributeMemory(const uint16_t x, const uint16_t y, const uint8_t value) {
    const uint16_t offset = xy_to_at_addr(x, y);

    PEEK(PPUSTATUS);
    POKE(PPUADDR, (uint8_t)(offset >> 8));
    POKE(PPUADDR, (uint8_t)(offset & 0xFF));
    POKE(PPUDATA, value);
}

oamBuffer_t oamBuffer __attribute__((aligned(256)));

void RefreshSprites(void) {
    POKE(OAMADDR, 0);
    POKE(OAMDMA, (uint16_t)oamBuffer >> 8);
}

uint16_t CartesianToAddress(uint16_t x, uint16_t y) {
    return xy_to_nt_addr(x, y);
}

scroll_t CartesianToScroll(uint16_t px, uint16_t py) {
    uint16_t y = py;
    if (y >= 240) { y -= 240; y ^= 0x100; }
    if (y >= 240) { y -= 240; y ^= 0x100; }
    uint8_t nt = (uint8_t)((px >> 8 & 0x01) | (y >> 7 & 0x02));
    return (scroll_t){{ (uint8_t)(SPPUCTRL & 0xFC | nt), (uint8_t)(px & 0xFF), (uint8_t)(y & 0xFF) }};
}

void SetColorPriority(const uint8_t priority) {
    SPPUMASK &= ~(RED | GREEN | BLUE);
    SPPUMASK |= priority & (RED | GREEN | BLUE);
    POKE(PPUMASK, SPPUMASK);
}

void WaitThenReactToSpriteZero(uint16_t px, uint16_t py, void (*fn)(void), atomic uint8_t* latch) {
    while (!*latch) {
        while (  PEEK(PPUSTATUS) & 0x40)  { }  // wait for pre-render to clear stale hit
        while (!(PEEK(PPUSTATUS) & 0x40)) { }  // wait for actual sprite 0 hit
        fn();
        *latch = true;
    }
}