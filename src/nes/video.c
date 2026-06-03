#include <platform-nes/video.h>
#include <platform-nes/technology.h>
#include <stdint.h>

const uint16_t PatternTables    = 0;
const uint16_t NameTables       = 0x2000;
const uint16_t PaletteTables    = 0x3f00;
const uint16_t nVideoRam        = 0x800;
atomic uint16_t xScroll = 0;
atomic uint16_t yScroll = 0;
atomic uint8_t SPPUCTRL;
atomic uint8_t SPPUMASK;

inline static uint16_t xy_to_nt_addr(uint16_t x, uint16_t y) {
    const auto base = 0x2000;
    const auto nt_h = ((x >> 5) & 1) << 10;
    const auto nt_v = (y / 30) << 11;
    const auto col  = x & 0x1F;
    const auto row  = (y % 30);

    return base + nt_h + nt_v + row * 32 + col;
}

inline static uint16_t xy_to_at_addr(uint16_t x, uint16_t y) {
    const auto base = 0x2000;
    const auto nt_h = ((x >> 5) & 1) << 10;
    const auto nt_v = (y / 30) << 11;
    const auto col  = x & 0x1F;
    const auto row  = (y % 30);

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

    for (auto page = 0; page < nVideoRam / 0x400; page++) {
        for (auto nt_hunk = 0; nt_hunk < 0xf0; nt_hunk++) {
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

__attribute__((hot))
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

    const auto nt = x >> 8 & 0x01
               | y >> 7 & 0x02;

    SPPUCTRL = (SPPUCTRL & 0xFC) | nt;
    POKE(PPUCTRL, SPPUCTRL);

    POKE(PPUSCROLL, (uint8_t)(x & 0xFF));
    POKE(PPUSCROLL, (uint8_t)(y & 0xFF));
}

void DeltaScroll(int8_t x, int8_t y) {
    SetScroll((uint16_t)(xScroll + x), (uint16_t)(yScroll + y));
}

__attribute__((hot))
void WriteBufferToVideoMemory(
    const uint16_t x, const uint16_t y, const uint8_t* source, const uint8_t sBuffer, const uint8_t polarity
) {
    const auto offset = xy_to_nt_addr(x, y);
    SPPUCTRL &= ~POLARITY;
    if (polarity) SPPUCTRL |= POLARITY;
    POKE(PPUCTRL, SPPUCTRL);

    PEEK(PPUSTATUS);
    POKE(PPUADDR, (uint8_t)(offset >> 8));
    POKE(PPUADDR, (uint8_t)(offset & 0xFF));

    for (auto i = 0; i < sBuffer; i++) {
        POKE(PPUDATA, source[i]);
    }
}

__attribute__((hot))
void WriteSingleToVideoMemory(const uint16_t x, const uint16_t y, uint8_t value) {
    const auto offset = xy_to_nt_addr(x, y);
    PEEK(PPUSTATUS);
    POKE(PPUADDR, (uint8_t)(offset >> 8));
    POKE(PPUADDR, (uint8_t)(offset & 0xFF));
    POKE(PPUDATA, value);
}

__attribute__((hot))
void WriteBufferToPaletteMemory(const uint8_t offset, const uint8_t* source, const uint8_t sBuffer) {
    PEEK(PPUSTATUS);
    POKE(PPUADDR, (uint8_t)((offset + PaletteTables) >> 8));
    POKE(PPUADDR, (uint8_t)( offset + PaletteTables  &  0xFF));

    for (auto i = 0; i < sBuffer; i++) {
        POKE(PPUDATA, source[i]);
    }
}

__attribute__((always_inline))
void WriteSingleToPaletteMemory(const uint8_t offset, uint8_t value) {
    PEEK(PPUSTATUS);
    POKE(PPUADDR, (uint8_t)((offset + PaletteTables) >> 8));
    POKE(PPUADDR, (uint8_t)( offset + PaletteTables  &  0xFF));
    POKE(PPUDATA, value);
}

__attribute__((hot))
void WriteProviderToVideoMemory(
    const uint16_t x, const uint16_t y, uint8_t (*fn)(uint16_t), const uint8_t amt, const uint8_t polarity
) {
    const auto offset = xy_to_nt_addr(x, y);
    SPPUCTRL &= ~POLARITY;
    if (polarity) SPPUCTRL |= POLARITY;

    POKE(PPUCTRL, SPPUCTRL);

    PEEK(PPUSTATUS);
    POKE(PPUADDR, (uint8_t)(offset >> 8));
    POKE(PPUADDR, (uint8_t)(offset & 0xFF));

    for (auto i = 0; i < amt; i++) {
        POKE(PPUDATA, fn(i));
    }
}

__attribute__((hot))
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

void StreamFromVideoMemory(const uint16_t offset, atomic uint8_t* target, const uint8_t size) {
    PEEK(PPUSTATUS);
    POKE(PPUADDR, (uint8_t)(offset >> 8));
    POKE(PPUADDR, (uint8_t)(offset & 0xFF));
    for (auto i = 0; i < size; i++) {
        target[i] = PEEK(PPUDATA);
    }
}

__attribute__((always_inline))
void WriteSingleToAttributeMemory(const uint16_t x, const uint16_t y, const uint8_t value) {
    const auto offset = xy_to_at_addr(x, y);

    PEEK(PPUSTATUS);
    POKE(PPUADDR, (uint8_t)(offset >> 8));
    POKE(PPUADDR, (uint8_t)(offset & 0xFF));
    POKE(PPUDATA, value);
}

__attribute__((always_inline))
void RefreshSprites(struct sprite_t *oam) {
    POKE(OAMDMA, (uint16_t)oam >> 8);
}

void OAMFromBuffer(struct sprite_t *oam, uint8_t slot, uint16_t off,
                   uint8_t width, const uint8_t *src, uint16_t count) {
    (void)width;                                   /* oam_t == uint8_t on NES */
    uint8_t *dst = (uint8_t *)oam + (uint16_t)slot * SPRITE_STRIDE + off;
    const uint8_t *s = src + off;
    for (uint16_t i = 0; i < count; i++)
        dst[i * SPRITE_STRIDE] = s[i * SPRITE_STRIDE];
}

void OAMFromProvider(struct sprite_t *oam, uint8_t slot, uint16_t off,
                     uint8_t width, oam_t (*fn)(uint16_t), uint16_t count) {
    (void)width;                                   /* oam_t == uint8_t on NES */
    uint8_t *base = (uint8_t *)oam + (uint16_t)slot * SPRITE_STRIDE + off;
    for (uint16_t i = 0; i < count; i++)
        base[i * SPRITE_STRIDE] = fn(i);
}

uint16_t CartesianToAddress(uint16_t x, uint16_t y) {
    return xy_to_nt_addr(x, y);
}

scroll_t CartesianToScroll(uint16_t px, uint16_t py) {
    auto y = py;
    if (y >= 240) { y -= 240; y ^= 0x100; }
    if (y >= 240) { y -= 240; y ^= 0x100; }
    const auto nt = (uint8_t)((px >> 8 & 0x01) | (y >> 7 & 0x02));
    return (scroll_t){{ (uint8_t)(SPPUCTRL & 0xFC | nt), (uint8_t)(px & 0xFF), (uint8_t)(y & 0xFF) }};
}

__attribute__((hot))
void SetColorPriority(const uint8_t priority) {
    SPPUMASK &= ~(RED | GREEN | BLUE);
    SPPUMASK |= priority & (RED | GREEN | BLUE);
    POKE(PPUMASK, SPPUMASK);
}

__attribute__((hot))
void WaitThenReactToSpriteZero(uint16_t px, uint16_t py, void (*fn)(void), atomic uint8_t* latch) {
    while (!*latch) {
        while (  PEEK(PPUSTATUS) & 0x40)  { }  // wait for pre-render to clear stale hit
        while (!(PEEK(PPUSTATUS) & 0x40)) { }  // wait for actual sprite 0 hit
        fn();
        *latch = true;
    }
}