#include <platform-nes/video.hpp>
#include <platform-nes/technology.hpp>
#include <cstdint>

const std::uint16_t PatternTables        = 0;
constexpr std::uint16_t NameTables       = 0x2000;
constexpr std::uint16_t PaletteTables    = 0x3f00;
constexpr std::uint16_t nVideoRam        = 0x800;
atomic std::uint16_t xScroll = 0;
atomic std::uint16_t yScroll = 0;
atomic std::uint8_t SPPUCTRL;
atomic std::uint8_t SPPUMASK;

inline static std::uint16_t xy_to_nt_addr(const std::uint16_t x, const std::uint16_t y) {
    constexpr auto base = 0x2000;
    const auto nt_h = (x >> 5 & 1) << 10;
    const auto nt_v = (y / 30) << 11;
    const auto col  = x & 0x1F;
    const auto row  = y % 30;

    return base + nt_h + nt_v + row * 32 + col;
}

inline static std::uint16_t xy_to_at_addr(const std::uint16_t x, const std::uint16_t y) {
    constexpr auto base = 0x2000;
    const auto nt_h = (x >> 5 & 1) << 10;
    const auto nt_v = (y / 30) << 11;
    const auto col  = x & 0x1F;
    const auto row  = y % 30;

    return base + nt_h + nt_v + 0x3C0 + (row / 4) * 8 + (col / 4);
}

// TODO: This is a bad name, it does not actually wait on NES and shouldn't for NES multithreading
//       a true 'wait for present' on NES would infinite loop, but won't return to main thread
//       without special return tech which we don't have yet
void WaitForPresent() {

}

void EnableRendering(const std::uint8_t ppuCtrl_, const std::uint8_t ppuMask_) {
    SPPUMASK = ppuMask_;
    SPPUCTRL = 0x80 | ppuCtrl_;
    POKE(PPUCTRL, SPPUCTRL);
    POKE(PPUMASK, SPPUMASK);
}

void FlushVideoRAM(const std::uint8_t nt, const std::uint8_t at) {
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
        for (std::uint8_t at_hunk = 0; at_hunk < 0x10; at_hunk++) {
            POKE(PPUDATA, at);
            POKE(PPUDATA, at);
            POKE(PPUDATA, at);
            POKE(PPUDATA, at);

        }
    }
}

__attribute__((hot))
void SetScroll(const std::uint16_t x, std::uint16_t y) {
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

    SPPUCTRL = SPPUCTRL & 0xFC | nt;
    POKE(PPUCTRL, SPPUCTRL);

    POKE(PPUSCROLL, static_cast<std::uint8_t>(x & 0xFF));
    POKE(PPUSCROLL, static_cast<std::uint8_t>(y & 0xFF));
}

void DeltaScroll(const int8_t x, const int8_t y) {
    SetScroll(xScroll + x, yScroll + y);
}

__attribute__((hot))
void WriteBufferToVideoMemory(
    const std::uint16_t x, const std::uint16_t y, const std::uint8_t* source, const std::uint8_t sBuffer, const std::uint8_t polarity
) {
    const auto offset = xy_to_nt_addr(x, y);
    SPPUCTRL = SPPUCTRL & ~POLARITY;
    if (polarity) SPPUCTRL = SPPUCTRL | POLARITY;
    POKE(PPUCTRL, SPPUCTRL);

    PEEK(PPUSTATUS);
    POKE(PPUADDR, static_cast<std::uint8_t>(offset >> 8));
    POKE(PPUADDR, static_cast<std::uint8_t>(offset & 0xFF));

    for (auto i = 0; i < sBuffer; i++) {
        POKE(PPUDATA, source[i]);
    }
}

__attribute__((hot))
void WriteSingleToVideoMemory(const std::uint16_t x, const std::uint16_t y, const std::uint8_t value) {
    const auto offset = xy_to_nt_addr(x, y);
    PEEK(PPUSTATUS);
    POKE(PPUADDR, static_cast<std::uint8_t>(offset >> 8));
    POKE(PPUADDR, static_cast<std::uint8_t>(offset & 0xFF));
    POKE(PPUDATA, value);
}

__attribute__((hot))
void WriteBufferToPaletteMemory(const std::uint8_t offset, const std::uint8_t* source, const std::uint8_t sBuffer) {
    PEEK(PPUSTATUS);
    POKE(PPUADDR, static_cast<std::uint8_t>((offset + PaletteTables) >> 8));
    POKE(PPUADDR, static_cast<std::uint8_t>(offset + PaletteTables & 0xFF));

    for (auto i = 0; i < sBuffer; i++) {
        POKE(PPUDATA, source[i]);
    }
}

__attribute__((always_inline))
void WriteSingleToPaletteMemory(const std::uint8_t offset, const std::uint8_t value) {
    PEEK(PPUSTATUS);
    POKE(PPUADDR, static_cast<std::uint8_t>((offset + PaletteTables) >> 8));
    POKE(PPUADDR, static_cast<std::uint8_t>(offset + PaletteTables & 0xFF));
    POKE(PPUDATA, value);
}

__attribute__((hot))
void WriteProviderToVideoMemory(
    const std::uint16_t x, const std::uint16_t y, std::uint8_t (*fn)(std::uint16_t), const std::uint8_t amt, const std::uint8_t polarity
) {
    const auto offset = xy_to_nt_addr(x, y);
    SPPUCTRL = SPPUCTRL & ~POLARITY;
    if (polarity) SPPUCTRL = SPPUCTRL | POLARITY;

    POKE(PPUCTRL, SPPUCTRL);

    PEEK(PPUSTATUS);
    POKE(PPUADDR, static_cast<std::uint8_t>(offset >> 8));
    POKE(PPUADDR, static_cast<std::uint8_t>(offset & 0xFF));

    for (auto i = 0; i < amt; i++) {
        POKE(PPUDATA, fn(i));
    }
}

__attribute__((hot))
void WriteBufferToAttributeMemory(
    const std::uint16_t x, const std::uint16_t y, const std::uint8_t* source, const std::uint8_t sBuffer, const std::uint8_t polarity
) {
    const std::uint16_t offset = xy_to_at_addr(x, y);

    SPPUCTRL = SPPUCTRL & ~POLARITY;
    POKE(PPUCTRL, SPPUCTRL);

    PEEK(PPUSTATUS);
    POKE(PPUADDR, static_cast<std::uint8_t>(offset >> 8));
    POKE(PPUADDR, static_cast<std::uint8_t>(offset & 0xFF));

    for (std::uint8_t i = 0; i < sBuffer; i++) {
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

void StreamFromVideoMemory(const std::uint16_t offset, atomic std::uint8_t* target, const std::uint8_t size) {
    PEEK(PPUSTATUS);
    POKE(PPUADDR, static_cast<std::uint8_t>(offset >> 8));
    POKE(PPUADDR, static_cast<std::uint8_t>(offset & 0xFF));
    for (auto i = 0; i < size; i++) {
        target[i] = PEEK(PPUDATA);
    }
}

__attribute__((always_inline))
void WriteSingleToAttributeMemory(const std::uint16_t x, const std::uint16_t y, const std::uint8_t value) {
    const auto offset = xy_to_at_addr(x, y);

    PEEK(PPUSTATUS);
    POKE(PPUADDR, static_cast<std::uint8_t>(offset >> 8));
    POKE(PPUADDR, static_cast<std::uint8_t>(offset & 0xFF));
    POKE(PPUDATA, value);
}

__attribute__((always_inline))
void RefreshSprites(const sprite_t *oam) {
    std::uint16_t addr;
    __builtin_memcpy(&addr, &oam, sizeof addr);
    POKE(OAMDMA, static_cast<std::uint8_t>(addr >> 8));
}

void OAMFromBuffer(sprite_t *oam, const std::uint8_t slot, const std::uint16_t off,
                   const std::uint8_t width, const std::uint8_t *src, const std::uint16_t count) {
    (void)width;
    std::uint8_t *dst = reinterpret_cast<std::uint8_t *>(oam) + static_cast<std::uint16_t>(slot) * SPRITE_STRIDE + off;
    const std::uint8_t *s = src + off;
    for (std::uint16_t i = 0; i < count; i++)
        dst[i * SPRITE_STRIDE] = s[i * SPRITE_STRIDE];
}

void OAMFromProvider(sprite_t *oam, const std::uint8_t slot, const std::uint16_t off,
                     const std::uint8_t width, oam_t (*fn)(std::uint16_t), const std::uint16_t count) {
    (void)width;
    std::uint8_t *base = reinterpret_cast<std::uint8_t *>(oam) + static_cast<std::uint16_t>(slot) * SPRITE_STRIDE + off;
    for (std::uint16_t i = 0; i < count; i++)
        base[i * SPRITE_STRIDE] = fn(i);
}

std::uint16_t CartesianToAddress(const std::uint16_t x, const std::uint16_t y) {
    return xy_to_nt_addr(x, y);
}

scroll_t CartesianToScroll(const std::uint16_t px, const std::uint16_t py) {
    auto y = py;
    if (y >= 240) { y -= 240; y ^= 0x100; }
    if (y >= 240) { y -= 240; y ^= 0x100; }
    const auto nt = static_cast<std::uint8_t>(px >> 8 & 0x01 | y >> 7 & 0x02);
    return (scroll_t){{ static_cast<std::uint8_t>(SPPUCTRL & 0xFC | nt),
        static_cast<std::uint8_t>(px & 0xFF), static_cast<std::uint8_t>(y & 0xFF) }
    };
}

__attribute__((hot))
void SetColorPriority(const std::uint8_t priority) {
    SPPUMASK = SPPUMASK & ~(RED | GREEN | BLUE);
    SPPUMASK = SPPUMASK | priority & (RED | GREEN | BLUE);
    POKE(PPUMASK, SPPUMASK);
}

__attribute__((hot))
void WaitThenReactToSpriteZero(const std::uint16_t px, const std::uint16_t py, void (*fn)(), atomic std::uint8_t* latch) {
    (void)px; (void)py;

    while (!*latch) {
        while (  PEEK(PPUSTATUS) & 0x40)  { }  // wait for pre-render to clear stale hit
        while (!(PEEK(PPUSTATUS) & 0x40)) { }  // wait for actual sprite 0 hit
        fn();
        *latch = true;
    }
}