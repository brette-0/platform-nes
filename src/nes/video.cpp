#include <platform-nes/video.hpp>
#include <platform-nes/technology.hpp>
#include <intsh>
using namespace br0::intsh;

const u16 PatternTables        = 0;
constexpr u16 NameTables       = 0x2000;
constexpr u16 PaletteTables    = 0x3f00;
constexpr u16 nVideoRam        = 0x800;
atomic u16 xScroll = 0;
atomic u16 yScroll = 0;
atomic u8 SPPUCTRL;
atomic u8 SPPUMASK;

inline static u16 xy_to_nt_addr(const u16 x, const u16 y) {
    constexpr auto base = 0x2000;
    const auto nt_h = (x >> 5 & 1) << 10;
    const auto nt_v = (y / 30) << 11;
    const auto col  = x & 0x1F;
    const auto row  = y % 30;

    return base + nt_h + nt_v + row * 32 + col;
}

inline static u16 xy_to_at_addr(const u16 x, const u16 y) {
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

void EnableRendering(const u8 ppuCtrl_, const u8 ppuMask_) {
    SPPUMASK = ppuMask_;
    SPPUCTRL = 0x80 | ppuCtrl_;
    POKE(PPUCTRL, SPPUCTRL);
    POKE(PPUMASK, SPPUMASK);
}

void FlushVideoRAM(const u8 nt, const u8 at) {
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
        for (u8 at_hunk = 0; at_hunk < 0x10; at_hunk++) {
            POKE(PPUDATA, at);
            POKE(PPUDATA, at);
            POKE(PPUDATA, at);
            POKE(PPUDATA, at);

        }
    }
}

__attribute__((hot))
void SetScroll(const u16 x, u16 y) {
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

    POKE(PPUSCROLL, static_cast<u8>(x & 0xFF));
    POKE(PPUSCROLL, static_cast<u8>(y & 0xFF));
}

void DeltaScroll(const i8 x, const i8 y) {
    SetScroll(xScroll + x, yScroll + y);
}

__attribute__((hot))
void WriteBufferToVideoMemory(
    const u16 x, const u16 y, const u8* source, const u8 sBuffer, const u8 polarity
) {
    const auto offset = xy_to_nt_addr(x, y);
    SPPUCTRL = SPPUCTRL & ~POLARITY;
    if (polarity) SPPUCTRL = SPPUCTRL | POLARITY;
    POKE(PPUCTRL, SPPUCTRL);

    PEEK(PPUSTATUS);
    POKE(PPUADDR, static_cast<u8>(offset >> 8));
    POKE(PPUADDR, static_cast<u8>(offset & 0xFF));

    for (auto i = 0; i < sBuffer; i++) {
        POKE(PPUDATA, source[i]);
    }
}

__attribute__((hot))
void WriteSingleToVideoMemory(const u16 x, const u16 y, const u8 value) {
    const auto offset = xy_to_nt_addr(x, y);
    PEEK(PPUSTATUS);
    POKE(PPUADDR, static_cast<u8>(offset >> 8));
    POKE(PPUADDR, static_cast<u8>(offset & 0xFF));
    POKE(PPUDATA, value);
}

__attribute__((hot))
void WriteBufferToPaletteMemory(const u8 offset, const u8* source, const u8 sBuffer) {
    PEEK(PPUSTATUS);
    POKE(PPUADDR, static_cast<u8>((offset + PaletteTables) >> 8));
    POKE(PPUADDR, static_cast<u8>(offset + PaletteTables & 0xFF));

    for (auto i = 0; i < sBuffer; i++) {
        POKE(PPUDATA, source[i]);
    }
}

__attribute__((always_inline))
void WriteSingleToPaletteMemory(const u8 offset, const u8 value) {
    PEEK(PPUSTATUS);
    POKE(PPUADDR, static_cast<u8>((offset + PaletteTables) >> 8));
    POKE(PPUADDR, static_cast<u8>(offset + PaletteTables & 0xFF));
    POKE(PPUDATA, value);
}

__attribute__((hot))
void WriteProviderToVideoMemory(
    const u16 x, const u16 y, u8 (*fn)(u16), const u8 amt, const u8 polarity
) {
    const auto offset = xy_to_nt_addr(x, y);
    SPPUCTRL = SPPUCTRL & ~POLARITY;
    if (polarity) SPPUCTRL = SPPUCTRL | POLARITY;

    POKE(PPUCTRL, SPPUCTRL);

    PEEK(PPUSTATUS);
    POKE(PPUADDR, static_cast<u8>(offset >> 8));
    POKE(PPUADDR, static_cast<u8>(offset & 0xFF));

    for (auto i = 0; i < amt; i++) {
        POKE(PPUDATA, fn(i));
    }
}

__attribute__((hot))
void WriteBufferToAttributeMemory(
    const u16 x, const u16 y, const u8* source, const u8 sBuffer, const u8 polarity
) {
    const u16 offset = xy_to_at_addr(x, y);

    SPPUCTRL = SPPUCTRL & ~POLARITY;
    POKE(PPUCTRL, SPPUCTRL);

    PEEK(PPUSTATUS);
    POKE(PPUADDR, static_cast<u8>(offset >> 8));
    POKE(PPUADDR, static_cast<u8>(offset & 0xFF));

    for (u8 i = 0; i < sBuffer; i++) {
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

void StreamFromVideoMemory(const u16 offset, atomic u8* target, const u8 size) {
    PEEK(PPUSTATUS);
    POKE(PPUADDR, static_cast<u8>(offset >> 8));
    POKE(PPUADDR, static_cast<u8>(offset & 0xFF));
    for (auto i = 0; i < size; i++) {
        target[i] = PEEK(PPUDATA);
    }
}

__attribute__((always_inline))
void WriteSingleToAttributeMemory(const u16 x, const u16 y, const u8 value) {
    const auto offset = xy_to_at_addr(x, y);

    PEEK(PPUSTATUS);
    POKE(PPUADDR, static_cast<u8>(offset >> 8));
    POKE(PPUADDR, static_cast<u8>(offset & 0xFF));
    POKE(PPUDATA, value);
}

__attribute__((always_inline))
void RefreshSprites(const sprite_t *oam) {
    u16 addr;
    __builtin_memcpy(&addr, &oam, sizeof addr);
    POKE(OAMDMA, static_cast<u8>(addr >> 8));
}

void OAMFromBuffer(sprite_t *oam, const u8 slot, const u16 off,
                   const u8 width, const u8 *src, const u16 count) {
    (void)width;
    u8 *dst = reinterpret_cast<u8 *>(oam) + static_cast<u16>(slot) * SPRITE_STRIDE + off;
    const u8 *s = src + off;
    for (u16 i = 0; i < count; i++)
        dst[i * SPRITE_STRIDE] = s[i * SPRITE_STRIDE];
}

void OAMFromProvider(sprite_t *oam, const u8 slot, const u16 off,
                     const u8 width, oam_t (*fn)(u16), const u16 count) {
    (void)width;
    u8 *base = reinterpret_cast<u8 *>(oam) + static_cast<u16>(slot) * SPRITE_STRIDE + off;
    for (u16 i = 0; i < count; i++)
        base[i * SPRITE_STRIDE] = fn(i);
}

u16 CartesianToAddress(const u16 x, const u16 y) {
    return xy_to_nt_addr(x, y);
}

scroll_t CartesianToScroll(const u16 px, const u16 py) {
    auto y = py;
    if (y >= 240) { y -= 240; y ^= 0x100; }
    if (y >= 240) { y -= 240; y ^= 0x100; }
    const auto nt = static_cast<u8>(px >> 8 & 0x01 | y >> 7 & 0x02);
    return (scroll_t){{ static_cast<u8>(SPPUCTRL & 0xFC | nt),
        static_cast<u8>(px & 0xFF), static_cast<u8>(y & 0xFF) }
    };
}

__attribute__((hot))
void SetColorPriority(const u8 priority) {
    SPPUMASK = SPPUMASK & ~(RED | GREEN | BLUE);
    SPPUMASK = SPPUMASK | priority & (RED | GREEN | BLUE);
    POKE(PPUMASK, SPPUMASK);
}

__attribute__((hot))
void WaitThenReactToSpriteZero(const u16 px, const u16 py, void (*fn)(), atomic u8* latch) {
    (void)px; (void)py;

    while (!*latch) {
        while (  PEEK(PPUSTATUS) & 0x40)  { }  // wait for pre-render to clear stale hit
        while (!(PEEK(PPUSTATUS) & 0x40)) { }  // wait for actual sprite 0 hit
        fn();
        *latch = true;
    }
}