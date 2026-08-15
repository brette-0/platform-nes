#include <platform-nes/video.hpp>
#include <platform-nes/technology.hpp>

// WHERE THIS MODULE'S CODE GOES -- the consuming project's choice, supplied as
// PLATFORM_NES_VIDEO_SECTION (see local.cmake.example). Unlike audio's, this is
// OPTIONAL: audio has to share a bank with the FamiStudio engine it plain-calls,
// so an unset section there is an #error. video has no such contract, so leaving
// it unset simply keeps today's behaviour -- inlined into callers, placed
// nowhere in particular.
//
// Setting it is a real tradeoff, not free: see ::MODULE_PLACEMENT. Every
// function below stops being inlined and becomes ~2 KiB of out-of-line code in
// the section named.
//
// EVERY definition is tagged, including the three that are also
// __attribute__((always_inline)) -- WriteSingleToAttributeTable, WriteSingle
// and RefreshSprites. That combination is NOT an error (verified: clang accepts
// section + always_inline + noinline together, no diagnostic); the tag is
// simply inert on them, because an always-inlined function emits no
// out-of-line copy for a section to place. Tagging them anyway keeps this file
// free of an exception nobody would remember, and costs nothing.
//
// Those three are also the one category that is bank-safe for free: a function
// copied into every caller is in reach of whatever bank the caller is in. The
// only wrinkle is that taking one's address does force an out-of-line copy,
// and that copy DOES honour the section.
#ifdef PLATFORM_NES_VIDEO_SECTION
#define VIDEO_BANK MODULE_PLACEMENT(PLATFORM_NES_VIDEO_SECTION)
#else
#define VIDEO_BANK
#endif
#include <intsh>
using namespace br0::intsh;

const u16 video::PatternTables = 0;
constexpr u16 NameTables       = 0x2000;
constexpr u16 PaletteTables    = 0x3f00;
constexpr u16 nVideoRam        = 0x800;
atomic u16 xScroll = 0;
atomic u16 yScroll = 0;
namespace ppu {
tech::wo_register<raw::PPUCTRL>   PPUCTRL;
tech::wo_register<raw::PPUMASK>   PPUMASK;
}   // namespace ppu

namespace oam {
tech::wo_register<ppu::raw::OAMADDR> OAMADDR;
tech::wo_register<ppu::raw::OAMDMA>  OAMDMA;
}   // namespace oam

inline static u16 xy_to_nt_addr(const u16 x, const u16 y) {
    constexpr u16 base = 0x2000;
    const u16 nt_h = static_cast<u16>((x >> 5 & 1) << 10);
    const u16 col  = x & 0x1F;
    if (y < 30) {
        return base + nt_h + (y << 5) + col;
    }
    const u16 nt_v = static_cast<u16>((y / 30) << 11);
    const u16 row  = y % 30;
    return base + nt_h + nt_v + row * 32 + col;
}

inline static u16 xy_to_at_addr(const u16 x, const u16 y) {
    constexpr u16 base = 0x2000;
    const u16 nt_h = static_cast<u16>((x >> 5 & 1) << 10);
    const u16 col  = x & 0x1F;
    if (y < 30) {
        return base + nt_h + 0x3C0 + ((y >> 2) << 3) + (col >> 2);
    }
    const u16 nt_v = static_cast<u16>((y / 30) << 11);
    const u16 row  = y % 30;
    return base + nt_h + nt_v + 0x3C0 + (row / 4) * 8 + (col / 4);
}

namespace video {

// TODO: This is a bad name, it does not actually wait on NES and shouldn't for NES multithreading
//       a true 'wait for present' on NES would infinite loop, but won't return to main thread
//       without special return tech which we don't have yet
VIDEO_BANK void WaitForPresent() {

}

}   // namespace video

namespace ppu {

VIDEO_BANK void EnableRendering(const u8 ppuCtrl_, const u8 ppuMask_) {
    PPUCTRL = 0x80 | ppuCtrl_;   // each assignment writes shadow + NMI enable
    PPUMASK = ppuMask_;
}

VIDEO_BANK void Flush(const u8 nt, const u8 at) {
    tech::peek(raw::PPUSTATUS);
    tech::poke(raw::PPUADDR, NameTables >> 8);
    tech::poke(raw::PPUADDR, NameTables & 0xFF);

    for (auto page = 0; page < nVideoRam / 0x400; page++) {
        for (auto nt_hunk = 0; nt_hunk < 0xf0; nt_hunk++) {
            tech::poke(raw::PPUDATA, nt);
            tech::poke(raw::PPUDATA, nt);
            tech::poke(raw::PPUDATA, nt);
            tech::poke(raw::PPUDATA, nt);
        }
        for (u8 at_hunk = 0; at_hunk < 0x10; at_hunk++) {
            tech::poke(raw::PPUDATA, at);
            tech::poke(raw::PPUDATA, at);
            tech::poke(raw::PPUDATA, at);
            tech::poke(raw::PPUDATA, at);

        }
    }
}

__attribute__((hot))
VIDEO_BANK void SetScroll(const u16 x, u16 y) {
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

    PPUCTRL = PPUCTRL & 0xFC | nt;

    tech::poke(raw::PPUSCROLL, static_cast<u8>(x & 0xFF));
    tech::poke(raw::PPUSCROLL, static_cast<u8>(y & 0xFF));
}

VIDEO_BANK void DeltaScroll(const i8 x, const i8 y) {
    SetScroll(xScroll + x, yScroll + y);
}

__attribute__((hot))
VIDEO_BANK void WriteFromBufferToNameTable(
    const u16 x, const u16 y, const u8* source, const u8 sBuffer, const u8 polarity
) {
    const auto offset = xy_to_nt_addr(x, y);
    u8 ctrl = PPUCTRL & ~ctrl::POLARITY;
    if (polarity) ctrl = ctrl | ctrl::POLARITY;
    PPUCTRL = ctrl;   // one write back: shadow + hardware

    tech::peek(raw::PPUSTATUS);
    tech::poke(raw::PPUADDR, static_cast<u8>(offset >> 8));
    tech::poke(raw::PPUADDR, static_cast<u8>(offset & 0xFF));

    for (auto i = 0; i < sBuffer; i++) {
        tech::poke(raw::PPUDATA, source[i]);
    }
}

__attribute__((hot))
VIDEO_BANK void WriteSingleToNameTable(const u16 x, const u16 y, const u8 value) {
    const auto offset = xy_to_nt_addr(x, y);
    tech::peek(raw::PPUSTATUS);
    tech::poke(raw::PPUADDR, static_cast<u8>(offset >> 8));
    tech::poke(raw::PPUADDR, static_cast<u8>(offset & 0xFF));
    tech::poke(raw::PPUDATA, value);
}

// Address overload: the caller already projected (x,y) -> a $2000-based PPU address
// (via CartesianToAddress), so this skips xy_to_nt_addr entirely -- just the latch
// reset and three pokes. Meant for the vblank window, where the divide/modulo in the
// (x,y) form is the cost worth hoisting out.
__attribute__((hot))
VIDEO_BANK void WriteSingleToNameTable(const int address, const u8 value) {
    tech::peek(raw::PPUSTATUS);
    tech::poke(raw::PPUADDR, static_cast<u8>(address >> 8));
    tech::poke(raw::PPUADDR, static_cast<u8>(address & 0xFF));
    tech::poke(raw::PPUDATA, value);
}

template <typename Idx>
__attribute__((hot))
VIDEO_BANK void WriteFromProviderToNameTable(
    const u16 x, const u16 y, u8 (*fn)(Idx), const u8 amt, const u8 polarity
) {
    const auto offset = xy_to_nt_addr(x, y);
    u8 ctrl = PPUCTRL & ~ctrl::POLARITY;
    if (polarity) ctrl = ctrl | ctrl::POLARITY;
    PPUCTRL = ctrl;   // one write back: shadow + hardware

    tech::peek(raw::PPUSTATUS);
    tech::poke(raw::PPUADDR, static_cast<u8>(offset >> 8));
    tech::poke(raw::PPUADDR, static_cast<u8>(offset & 0xFF));

    for (Idx i = 0; i < amt; ++i) {
        tech::poke(raw::PPUDATA, fn(i));
    }
}

// Explicit instantiations for the provider index types in use. The body pokes
// PPU registers, so it must stay in this backend rather than the header.
template void WriteFromProviderToNameTable<u8>(u16, u16, u8 (*)(u8), u8, u8);
template void WriteFromProviderToNameTable<u16>(u16, u16, u8 (*)(u16), u8, u8);

__attribute__((hot))
VIDEO_BANK void WriteFromBufferToAttributeTable(
    const u16 x, const u16 y, const u8* source, const u8 sBuffer, const u8 polarity
) {
    const u16 offset = xy_to_at_addr(x, y);

    PPUCTRL = PPUCTRL & ~ctrl::POLARITY;

    if (polarity) {
        // AT rows for the same tile column are 8 bytes apart.  Re-address each
        // byte explicitly (peek PPUSTATUS + 2 pokes = 12 cycles/byte) instead of
        // seven dummy PPUDATA reads (7×4 = 28 cycles/byte) to stride between rows.
        // base_lo + i*8 stays ≤ $FF for sBuffer≤8 (base_lo ≤ $C7, 7×8 = $38).
        const u8 hi = static_cast<u8>(offset >> 8);
        const u8 lo = static_cast<u8>(offset & 0xFF);
        for (u8 i = 0; i < sBuffer; i++) {
            tech::peek(raw::PPUSTATUS);
            tech::poke(raw::PPUADDR, hi);
            tech::poke(raw::PPUADDR, static_cast<u8>(lo + (i << 3)));
            tech::poke(raw::PPUDATA, source[i]);
        }
        return;
    }

    tech::peek(raw::PPUSTATUS);
    tech::poke(raw::PPUADDR, static_cast<u8>(offset >> 8));
    tech::poke(raw::PPUADDR, static_cast<u8>(offset & 0xFF));
    for (u8 i = 0; i < sBuffer; i++) {
        tech::poke(raw::PPUDATA, source[i]);
    }
}

__attribute__((always_inline))
VIDEO_BANK void WriteSingleToAttributeTable(const u16 x, const u16 y, const u8 value) {
    const auto offset = xy_to_at_addr(x, y);

    tech::peek(raw::PPUSTATUS);
    tech::poke(raw::PPUADDR, static_cast<u8>(offset >> 8));
    tech::poke(raw::PPUADDR, static_cast<u8>(offset & 0xFF));
    tech::poke(raw::PPUDATA, value);
}

VIDEO_BANK u16 CartesianToAddress(const u16 x, const u16 y) {
    return xy_to_nt_addr(x, y);
}

scroll_t CartesianToScroll(const u16 px, const u16 py) {
    auto y = py;
    if (y >= 240) { y -= 240; y ^= 0x100; }
    if (y >= 240) { y -= 240; y ^= 0x100; }
    const auto nt = static_cast<u8>(px >> 8 & 0x01 | y >> 7 & 0x02);
    return (scroll_t){{ static_cast<u8>(PPUCTRL & 0xFC | nt),
        static_cast<u8>(px & 0xFF), static_cast<u8>(y & 0xFF) }
    };
}

__attribute__((hot))
VIDEO_BANK void SetColorPriority(const u8 priority) {
    u8 mask = PPUMASK & ~(mask::RED | mask::GREEN | mask::BLUE);
    mask = mask | priority & (mask::RED | mask::GREEN | mask::BLUE);
    PPUMASK = mask;   // one write back: shadow + hardware
}

namespace pal {

__attribute__((hot))
VIDEO_BANK void WriteFromBuffer(const u8 offset, const u8* source, const u8 sBuffer) {
    tech::peek(raw::PPUSTATUS);
    tech::poke(raw::PPUADDR, static_cast<u8>((offset + PaletteTables) >> 8));
    tech::poke(raw::PPUADDR, static_cast<u8>(offset + PaletteTables & 0xFF));

    for (auto i = 0; i < sBuffer; i++) {
        tech::poke(raw::PPUDATA, source[i]);
    }
}

__attribute__((always_inline))
VIDEO_BANK void WriteSingle(const u8 offset, const u8 value) {
    tech::peek(raw::PPUSTATUS);
    tech::poke(raw::PPUADDR, static_cast<u8>((offset + PaletteTables) >> 8));
    tech::poke(raw::PPUADDR, static_cast<u8>(offset + PaletteTables & 0xFF));
    tech::poke(raw::PPUDATA, value);
}

}   // namespace pal

}   // namespace ppu

namespace oam {

__attribute__((always_inline))
VIDEO_BANK void RefreshSprites(const sprite_t *oam) {
    u16 addr;
    __builtin_memcpy(&addr, &oam, sizeof addr);
    tech::poke(ppu::raw::OAMDMA, static_cast<u8>(addr >> 8));
}

VIDEO_BANK void OAMFromBuffer(sprite_t *oam, const u8 slot, const u16 off,
                   const u8 width, const u8 *src, const u16 count) {
    (void)width;
    u8 *dst = reinterpret_cast<u8 *>(oam) + static_cast<u16>(slot) * spriteStride + off;
    const u8 *s = src + off;
    for (u16 i = 0; i < count; i++)
        dst[i * spriteStride] = s[i * spriteStride];
}

VIDEO_BANK void OAMFromProvider(sprite_t *oam, const u8 slot, const u16 off,
                     const u8 width, oam_t (*fn)(u16), const u16 count) {
    (void)width;
    u8 *base = reinterpret_cast<u8 *>(oam) + static_cast<u16>(slot) * spriteStride + off;
    for (u16 i = 0; i < count; i++)
        base[i * spriteStride] = fn(i);
}

}   // namespace oam

namespace ppu {

VIDEO_BANK void StreamFromVideoMemory(const u16 offset, atomic u8* target, const u8 size) {
    tech::peek(raw::PPUSTATUS);
    tech::poke(raw::PPUADDR, static_cast<u8>(offset >> 8));
    tech::poke(raw::PPUADDR, static_cast<u8>(offset & 0xFF));
    for (auto i = 0; i < size; i++) {
        target[i] = tech::peek(raw::PPUDATA);
    }
}

}   // namespace ppu

__attribute__((hot))
VIDEO_BANK void video::WaitThenReactToSpriteZero(const u16 px, const u16 py, void (*fn)(), atomic u8* latch) {
    (void)px; (void)py;

    while (!*latch) {
        while (  tech::peek(ppu::raw::PPUSTATUS) & 0x40)  { }  // wait for pre-render to clear stale hit
        while (!(tech::peek(ppu::raw::PPUSTATUS) & 0x40)) { }  // wait for actual sprite 0 hit
        fn();
        *latch = true;
    }
}
