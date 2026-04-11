#ifndef VIDEO_H
#define VIDEO_H

#include <stdint.h>

/*
 * Section directives per target.
 *
 * NES  : .chr_rom       — consumed by the mapper hardware
 * Win  : chr_rom$m      — PE/COFF, sorts after the library's $a anchor
 * macOS: __DATA,chr_rom — Mach-O segment,section pair
 * Linux: chr_rom        — ELF, C-identifier name so linker emits __start_
 */
#ifdef TARGET_NES
  #define _CHR_PUSH  ".pushsection .chr_rom,\"a\"\n"
#elif defined(_WIN32)
  #define _CHR_PUSH  ".pushsection chr_rom$m,\"dr\"\n"
#elif defined(__APPLE__)
  #define _CHR_PUSH  ".pushsection __DATA,chr_rom\n"
#else
  #define _CHR_PUSH  ".pushsection chr_rom,\"a\"\n"
#endif

#define _CHR_POP ".popsection\n"

#define CHARACTER_ROM(name, path)                \
__asm__(                                         \
_CHR_PUSH                                        \
".global " #name "_start\n"                      \
".global " #name "_end\n"                        \
#name "_start:\n"                                \
".incbin \"" path "\"\n"                         \
#name "_end:\n"                                  \
_CHR_POP                                         \
);                                               \
extern const uint8_t name##_start[];             \
extern const uint8_t name##_end[];

#ifdef TARGET_NES
  #define CHARACTER_ROM_ALIGN(addr) __attribute__((section(".chr_rom"), aligned(addr)))
#elif defined(_WIN32)
  #define CHARACTER_ROM_ALIGN(addr) __attribute__((section("chr_rom$m"), aligned(addr)))
#elif defined(__APPLE__)
  #define CHARACTER_ROM_ALIGN(addr) __attribute__((section("__DATA,chr_rom"), aligned(addr)))
#else
  #define CHARACTER_ROM_ALIGN(addr) __attribute__((section("chr_rom"), aligned(addr)))
#endif

/* Per-asset accessors */
#define CHR(name)       ((const uint8_t *)(name##_start))
#define CHR_SIZE(name)  ((size_t)(name##_end - name##_start))

/*
 * CHR_ROM — pointer to byte 0 of the entire character ROM hunk.
 *
 * Win  : _chr_rom is emitted by the library into chr_rom$a (sorts first).
 * macOS: linker-provided section boundary symbol.
 * Linux: linker-provided __start_chr_rom for C-identifier sections.
 * NES  : not exposed (PPU addresses CHR ROM directly via hardware).
 */
#ifndef TARGET_NES
  #if defined(_WIN32)
    extern const uint8_t _chr_rom[];
  #elif defined(__APPLE__)
    extern const uint8_t _chr_rom[] __asm("section$start$__DATA$chr_rom");
  #else
    extern const uint8_t _chr_rom[] __asm("__start_chr_rom");
  #endif
  #define CHR_ROM ((const uint8_t *)_chr_rom)
#endif

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

void WaitForPresent();
void EnableRendering(uint8_t ppuMask);

#ifndef TARGET_NES
extern const uint8_t* VideoRAM;
#endif


#endif
