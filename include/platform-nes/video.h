#ifndef VIDEO_H
#define VIDEO_H

#include <stdint.h>
#include <stddef.h>

#ifndef TARGET_NES
#include <SDL3/SDL_video.h>
#endif

extern const uint16_t PatternTables;

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

#define CHARACTER_ROM_PAD(count, val)          \
__asm__(                                       \
_CHR_PUSH                                      \
".fill " #count ", 1, " #val "\n"              \
_CHR_POP                                       \
)

#ifndef TARGET_NES
typedef uint16_t oam_t;
#else
typedef uint8_t oam_t;
#endif

struct sprite_t {
  oam_t x;
  oam_t y;
  uint8_t attributes;
  uint8_t tile;
};

#ifdef TARGET_NES
  typedef struct sprite_t oamBuffer_t[64];
  #define sOAM 64
#else
  typedef struct {
      struct sprite_t* data;
      size_t           count;
      size_t           cap;
  } oamBuffer_t;
  extern size_t sOAM;
#endif

extern oamBuffer_t oamBuffer;

#ifdef TARGET_NES
  #define CHARACTER_ROM_ALIGN(addr) __attribute__((section(".chr_rom"), aligned(addr)))
#elif defined(_WIN32)
  #define CHARACTER_ROM_ALIGN(addr) __attribute__((section("chr_rom$m"), aligned(addr)))
#elif defined(__APPLE__)
  #define CHARACTER_ROM_ALIGN(addr) __attribute__((section("__DATA,chr_rom"), aligned(addr)))
#else
  #define CHARACTER_ROM_ALIGN(addr) __attribute__((section("chr_rom"), aligned(addr)))
#endif

#define CHR(name)       ((const uint8_t *)(name##_start))
#define CHR_SIZE(name)  ((size_t)(name##_end - name##_start))

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

enum CTRL {
    POLARITY    = 0x04,
    BG_ADDR     = 0x10,
    SPRITE_ADDR = 0x08
};


enum MASK {
    BG        = 0x08,
    SPRITE    = 0x10,
    BG_L      = 0x0a,
    SPRITE_L  = 0x14
};

void WaitForPresent();
void EnableRendering(uint8_t ppuCtrl_, uint8_t ppuMask_);

#ifdef TARGET_NES
#define VIEWPORT_X  32
#define VIEWPORT_Y  30
#else
extern const SDL_DisplayMode* mode;
extern uint8_t scale;

#define VIEWPORT_X  \
  ((mode->w / scale) >> 3)
#define VIEWPORT_Y  \
  ((mode->h / scale) >> 3)
#endif



#ifndef TARGET_NES
extern uint8_t* VideoRAM;
extern uint8_t* paletteRAM;
extern uint16_t xScroll;
extern uint16_t yScroll;
#endif

/**
 * Scrolls the screen in signed X and Y
 * @param x x position to move the screen scroll by
 * @param y y position to move the screen scroll by
 */
void DeltaScroll(int8_t x, int8_t y);

/**
 * Sets the current scroll of the screen
 * @param x x position to set the scroll to
 * @param y y position to set the scroll to
 */
void SetScroll(uint16_t x, uint16_t y);

/**
 * Writes from CPU to video RAM with an array of elements with specified polarity
 * @param source    Source of information to push into PPU video RAM
 * @param sBuffer   size of source buffer
 * @param polarity  writes horizontal or vertical
 */
void WriteBufferToVideoMemory(
  const uint16_t x, const uint16_t y, const uint8_t* source, uint8_t sBuffer, uint8_t polarity
);

void FlushVideoRAM(const uint8_t nt, const uint8_t at);

void WriteBufferToPaletteMemory(const uint8_t offset, const uint8_t* source, uint8_t sBuffer);

void WriteProviderToVideoMemory(uint16_t x, const uint16_t y, uint8_t (*fn)(uint8_t), uint8_t amt, uint8_t polarity);

void WriteBufferToAttributeMemory(
  const uint16_t x, const uint16_t y, const uint8_t* source, const uint8_t sBuffer, uint8_t polarity
);

void RefreshSprites(void);

#endif
