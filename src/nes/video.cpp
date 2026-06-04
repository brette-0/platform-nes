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
wo_register<ppu::raw::PPUCTRL> SPPUCTRL;
wo_register<ppu::raw::PPUMASK> SPPUMASK;

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

namespace video {

// TODO: This is a bad name, it does not actually wait on NES and shouldn't for NES multithreading
//       a true 'wait for present' on NES would infinite loop, but won't return to main thread
//       without special return tech which we don't have yet
void WaitForPresent() {

}

}   // namespace video

namespace ppu {

void EnableRendering(const u8 ppuCtrl_, const u8 ppuMask_) {
    SPPUCTRL = 0x80 | ppuCtrl_;   // each assignment writes shadow + hardware
    SPPUMASK = ppuMask_;
}

void Flush(const u8 nt, const u8 at) {
    peek(ppu::raw::PPUSTATUS);
    poke(ppu::raw::PPUADDR, NameTables >> 8);
    poke(ppu::raw::PPUADDR, NameTables & 0xFF);

    for (auto page = 0; page < nVideoRam / 0x400; page++) {
        for (auto nt_hunk = 0; nt_hunk < 0xf0; nt_hunk++) {
            poke(ppu::raw::PPUDATA, nt);
            poke(ppu::raw::PPUDATA, nt);
            poke(ppu::raw::PPUDATA, nt);
            poke(ppu::raw::PPUDATA, nt);
        }
        for (u8 at_hunk = 0; at_hunk < 0x10; at_hunk++) {
            poke(ppu::raw::PPUDATA, at);
            poke(ppu::raw::PPUDATA, at);
            poke(ppu::raw::PPUDATA, at);
            poke(ppu::raw::PPUDATA, at);

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

    poke(ppu::raw::PPUSCROLL, static_cast<u8>(x & 0xFF));
    poke(ppu::raw::PPUSCROLL, static_cast<u8>(y & 0xFF));
}

void DeltaScroll(const i8 x, const i8 y) {
    SetScroll(xScroll + x, yScroll + y);
}

__attribute__((hot))
void WriteFromBufferToNameTable(
    const u16 x, const u16 y, const u8* source, const u8 sBuffer, const u8 polarity
) {
    const auto offset = xy_to_nt_addr(x, y);
    u8 ctrl = SPPUCTRL & ~ppu::ctrl::POLARITY;
    if (polarity) ctrl = ctrl | ppu::ctrl::POLARITY;
    SPPUCTRL = ctrl;   // one write back: shadow + hardware

    peek(ppu::raw::PPUSTATUS);
    poke(ppu::raw::PPUADDR, static_cast<u8>(offset >> 8));
    poke(ppu::raw::PPUADDR, static_cast<u8>(offset & 0xFF));

    for (auto i = 0; i < sBuffer; i++) {
        poke(ppu::raw::PPUDATA, source[i]);
    }
}

__attribute__((hot))
void WriteSingleToNameTable(const u16 x, const u16 y, const u8 value) {
    const auto offset = xy_to_nt_addr(x, y);
    peek(ppu::raw::PPUSTATUS);
    poke(ppu::raw::PPUADDR, static_cast<u8>(offset >> 8));
    poke(ppu::raw::PPUADDR, static_cast<u8>(offset & 0xFF));
    poke(ppu::raw::PPUDATA, value);
}

__attribute__((hot))
void WriteFromProviderToNameTable(
    const u16 x, const u16 y, u8 (*fn)(u16), const u8 amt, const u8 polarity
) {
    const auto offset = xy_to_nt_addr(x, y);
    u8 ctrl = SPPUCTRL & ~ppu::ctrl::POLARITY;
    if (polarity) ctrl = ctrl | ppu::ctrl::POLARITY;
    SPPUCTRL = ctrl;   // one write back: shadow + hardware

    peek(ppu::raw::PPUSTATUS);
    poke(ppu::raw::PPUADDR, static_cast<u8>(offset >> 8));
    poke(ppu::raw::PPUADDR, static_cast<u8>(offset & 0xFF));

    for (auto i = 0; i < amt; i++) {
        poke(ppu::raw::PPUDATA, fn(i));
    }
}

__attribute__((hot))
void WriteFromBufferToAttributeTable(
    const u16 x, const u16 y, const u8* source, const u8 sBuffer, const u8 polarity
) {
    const u16 offset = xy_to_at_addr(x, y);

    SPPUCTRL = SPPUCTRL & ~ppu::ctrl::POLARITY;

    peek(ppu::raw::PPUSTATUS);
    poke(ppu::raw::PPUADDR, static_cast<u8>(offset >> 8));
    poke(ppu::raw::PPUADDR, static_cast<u8>(offset & 0xFF));

    for (u8 i = 0; i < sBuffer; i++) {
        poke(ppu::raw::PPUDATA, source[i]);
        if (polarity) {
            peek(ppu::raw::PPUDATA);
            peek(ppu::raw::PPUDATA);
            peek(ppu::raw::PPUDATA);
            peek(ppu::raw::PPUDATA);
            peek(ppu::raw::PPUDATA);
            peek(ppu::raw::PPUDATA);
            peek(ppu::raw::PPUDATA);
        }
    }
}

__attribute__((always_inline))
void WriteSingleToAttributeTable(const u16 x, const u16 y, const u8 value) {
    const auto offset = xy_to_at_addr(x, y);

    peek(ppu::raw::PPUSTATUS);
    poke(ppu::raw::PPUADDR, static_cast<u8>(offset >> 8));
    poke(ppu::raw::PPUADDR, static_cast<u8>(offset & 0xFF));
    poke(ppu::raw::PPUDATA, value);
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
    u8 mask = SPPUMASK & ~(ppu::mask::RED | ppu::mask::GREEN | ppu::mask::BLUE);
    mask = mask | priority & (ppu::mask::RED | ppu::mask::GREEN | ppu::mask::BLUE);
    SPPUMASK = mask;   // one write back: shadow + hardware
}

namespace pal {

__attribute__((hot))
void WriteFromBuffer(const u8 offset, const u8* source, const u8 sBuffer) {
    peek(ppu::raw::PPUSTATUS);
    poke(ppu::raw::PPUADDR, static_cast<u8>((offset + PaletteTables) >> 8));
    poke(ppu::raw::PPUADDR, static_cast<u8>(offset + PaletteTables & 0xFF));

    for (auto i = 0; i < sBuffer; i++) {
        poke(ppu::raw::PPUDATA, source[i]);
    }
}

__attribute__((always_inline))
void WriteSingle(const u8 offset, const u8 value) {
    peek(ppu::raw::PPUSTATUS);
    poke(ppu::raw::PPUADDR, static_cast<u8>((offset + PaletteTables) >> 8));
    poke(ppu::raw::PPUADDR, static_cast<u8>(offset + PaletteTables & 0xFF));
    poke(ppu::raw::PPUDATA, value);
}

}   // namespace pal

}   // namespace ppu

namespace oam {

__attribute__((always_inline))
void RefreshSprites(const sprite_t *oam) {
    u16 addr;
    __builtin_memcpy(&addr, &oam, sizeof addr);
    poke(ppu::raw::OAMDMA, static_cast<u8>(addr >> 8));
}

void OAMFromBuffer(sprite_t *oam, const u8 slot, const u16 off,
                   const u8 width, const u8 *src, const u16 count) {
    (void)width;
    u8 *dst = reinterpret_cast<u8 *>(oam) + static_cast<u16>(slot) * spriteStride + off;
    const u8 *s = src + off;
    for (u16 i = 0; i < count; i++)
        dst[i * spriteStride] = s[i * spriteStride];
}

void OAMFromProvider(sprite_t *oam, const u8 slot, const u16 off,
                     const u8 width, oam_t (*fn)(u16), const u16 count) {
    (void)width;
    u8 *base = reinterpret_cast<u8 *>(oam) + static_cast<u16>(slot) * spriteStride + off;
    for (u16 i = 0; i < count; i++)
        base[i * spriteStride] = fn(i);
}

}   // namespace oam

namespace ppu {

void StreamFromVideoMemory(const u16 offset, atomic u8* target, const u8 size) {
    peek(ppu::raw::PPUSTATUS);
    poke(ppu::raw::PPUADDR, static_cast<u8>(offset >> 8));
    poke(ppu::raw::PPUADDR, static_cast<u8>(offset & 0xFF));
    for (auto i = 0; i < size; i++) {
        target[i] = peek(ppu::raw::PPUDATA);
    }
}

}   // namespace ppu

__attribute__((hot))
void WaitThenReactToSpriteZero(const u16 px, const u16 py, void (*fn)(), atomic u8* latch) {
    (void)px; (void)py;

    while (!*latch) {
        while (  peek(ppu::raw::PPUSTATUS) & 0x40)  { }  // wait for pre-render to clear stale hit
        while (!(peek(ppu::raw::PPUSTATUS) & 0x40)) { }  // wait for actual sprite 0 hit
        fn();
        *latch = true;
    }
}
