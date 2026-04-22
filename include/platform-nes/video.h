#ifndef VIDEO_H
#define VIDEO_H

#include <stdint.h>
#include <stddef.h>

#include "technology.h"

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
  oam_t y;
  uint8_t tile;
  uint8_t attributes;
  oam_t x;
};

#ifdef TARGET_NES
  typedef struct sprite_t oamBuffer_t[64];
  #define sOAM 64
  #define OAM_BUFFER oamBuffer
#else
  typedef struct {
      struct sprite_t* data;
      size_t           count;
      size_t           cap;
  } oamBuffer_t;
  extern size_t sOAM;
#define OAM_BUFFER oamBuffer.data
#endif

extern oamBuffer_t oamBuffer;

// Force alignment of the current insertion point inside the chr_rom
// section. Emits a .balign directive inside _CHR_PUSH/_CHR_POP so the
// next bytes added to the section (via CHARACTER_ROM, etc.) start at
// an `addr`-byte boundary. Required by the PPU / emulated PPU, which
// maps CHR pages on fixed 8 KB boundaries.
#define CHARACTER_ROM_ALIGN(addr)                \
__asm__(                                         \
_CHR_PUSH                                        \
".balign " #addr "\n"                            \
_CHR_POP                                         \
)

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
    GEN_NMI     = 0x80,
    POLARITY    = 0x04,
    BG_ADDR     = 0x10,
    SPRITE_ADDR = 0x08
};


enum MASK {
    BG             = 0x08,
    SPRITE         = 0x10,
    BG_L           = 0x0a,
    SPRITE_L       = 0x14,
    RED            = 0x20,
    GREEN          = 0x40,
    BLUE           = 0x80,
};

enum PALETTE {
    BG_0          = 0 << 2,
    BG_1          = 1 << 2,
    BG_2          = 2 << 2,
    BG_3          = 3 << 2,
    SPRITE_0      = 4 << 2,
    SPRITE_1      = 5 << 2,
    SPRITE_2      = 6 << 2,
    SPRITE_3      = 7 << 2,
};

void WaitForPresent();
void EnableRendering(uint8_t ppuCtrl_, uint8_t ppuMask_);

#ifdef TARGET_NES
#define VIEWPORT_TX  32
#define VIEWPORT_TY  30
#else
extern const SDL_DisplayMode* mode;
extern uint8_t scale;

#define VIEWPORT_TX  \
  (((mode->w / scale) >> 3) & ~3u)
#define VIEWPORT_TY  \
  ((mode->h / scale) >> 3)
#endif

#define VIEWPORT_PX  (VIEWPORT_TX << 3)
#define VIEWPORT_PY  (VIEWPORT_TY << 3)


#ifndef TARGET_NES
extern uint8_t* VideoRAM;
extern uint8_t* paletteRAM;
extern uint16_t xScroll;
extern uint16_t yScroll;
#endif

#ifdef TARGET_NES
typedef struct { uint8_t data[3]; } scroll_t;
/* data[0] = PPUCTRL byte (nametable select merged in)
 * data[1] = fine X scroll  (px & 0xFF)
 * data[2] = fine Y scroll  (py % 240 after nt-wrap) */
#else
typedef struct { uint16_t x; uint16_t y; } vec2u16;
typedef vec2u16 scroll_t;
#endif

/**
 * Converts a pixel position into the platform scroll_t representation.
 * NES:  encodes nametable select + fine X/Y into the 3-byte PPU format.
 * SDL3: stores px/py directly.
 */
scroll_t CartesianToScroll(uint16_t px, uint16_t py);

#ifdef TARGET_NES
  #define WRITE_SCROLL(s) do { \
      (*(volatile uint8_t*)PPUCTRL)   = (s).data[0]; \
      (*(volatile uint8_t*)PPUSCROLL) = (s).data[1]; \
      (*(volatile uint8_t*)PPUSCROLL) = (s).data[2]; \
  } while(0)
#else
  #define WRITE_SCROLL(s) do { xScroll = (s).x; yScroll = (s).y; } while(0)
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

void WriteSingleToVideoMemory(const uint16_t x, const uint16_t y, uint8_t value);

void FlushVideoRAM(const uint8_t nt, const uint8_t at);

void WriteBufferToPaletteMemory(const uint8_t offset, const uint8_t* source, uint8_t sBuffer);

void WriteSingleToPaletteMemory(const uint8_t offset, uint8_t value);

void WriteProviderToVideoMemory(uint16_t x, const uint16_t y, uint8_t (*fn)(uint16_t), uint8_t amt, uint8_t polarity);

uint16_t CartesianToAddress(uint16_t x, uint16_t y);

void WriteBufferToAttributeMemory(
  const uint16_t x, const uint16_t y, const uint8_t* source, const uint8_t sBuffer, uint8_t polarity
);

void WriteSingleToAttributeMemory(const uint16_t x, const uint16_t y, uint8_t value);

void RefreshSprites(void);

/**
 * Sets the color emphasis bits in PPUMASK (bits 5-7).
 * @param priority  OR of COLOR_EMPHASIS-field bits (0x20 = red, 0x40 = green, 0x80 = blue)
 */
void SetColorPriority(uint8_t priority);

#ifdef TARGET_NES
typedef void (*spriteZeroHandler_t)(void);
#else
typedef struct {
  void (*method)(void);
  uint16_t px;
  uint16_t py;
} spriteZeroHandler_t;

void SetSpriteZeroHandler(uint16_t px, uint16_t py, void (*fn)(void));
#define SET_SPRITE_ZERO_HANDLER(px, py, fn) SetSpriteZeroHandler(px, py, fn)
#endif

void WaitThenReactToSpriteZero(uint16_t px, uint16_t py, void (*fn)(void), atomic uint8_t* latch);

#ifdef TARGET_NES
extern atomic uint8_t SPPUCTRL;
extern atomic uint8_t SPPUMASK;

#define VRAM                                \
for (                                       \
uint8_t i = (POKE(PPUMASK, 0), 0);\
__builtin_expect(i < 1, 1);                 \
i++, POKE(PPUMASK, SPPUMASK))

#else
#define VRAM \
if (1)

#endif

#endif
