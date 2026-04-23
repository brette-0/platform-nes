/**
 * @file video.h
 * @brief PPU abstraction: CHR ROM embedding, OAM, scrolling,
 *        nametable / attribute / palette writes, sprite-zero events,
 *        and viewport geometry.
 *
 * The NES build targets the real PPU registers at \$2000-\$2007 and
 * OAM DMA at \$4014. The SDL3 build presents an equivalent software
 * framebuffer with the same register conventions, so application code
 * can compile unchanged against either backend.
 *
 * Two areas benefit from extra care:
 *
 * - **CHR ROM linkage.** The ::CHARACTER_ROM macro places tile data in
 *   a platform-specific section (`.chr_rom` on NES, `__DATA,chr_rom`
 *   on Mach-O, etc.) so the linker can lay it out on the 8 KB
 *   boundary the PPU requires. Use ::CHARACTER_ROM_ALIGN to force
 *   that alignment explicitly.
 * - **Video-memory writes.** Register-poking the PPU outside VBlank
 *   corrupts the display. The ::VRAM block wraps a safe access window
 *   by toggling ::PPUMASK around the body.
 */
#ifndef VIDEO_H
#define VIDEO_H

#include <stdint.h>
#include <stddef.h>

#include "technology.h"

#ifndef TARGET_NES
#include <SDL3/SDL_video.h>
#endif

/** @brief Base PPU address of the pattern tables (\$0000 / \$1000). */
extern const uint16_t PatternTables;

#ifdef TARGET_NES
  /** @brief Assembler `.pushsection` directive for CHR ROM on NES. */
  #define _CHR_PUSH  ".pushsection .chr_rom,\"a\"\n"
#elif defined(_WIN32)
  #define _CHR_PUSH  ".pushsection chr_rom$m,\"dr\"\n"
#elif defined(__APPLE__)
  #define _CHR_PUSH  ".pushsection __DATA,chr_rom\n"
#else
  #define _CHR_PUSH  ".pushsection chr_rom,\"a\"\n"
#endif

/** @brief Assembler `.popsection` directive complementing ::_CHR_PUSH. */
#define _CHR_POP ".popsection\n"

/**
 * @brief Embeds a `.chr`/binary file into the CHR ROM section.
 *
 * Emits `<name>_start` and `<name>_end` labels around the bytes
 * included from @p path, and declares them as extern arrays so C code
 * can access them via ::CHR and ::CHR_SIZE.
 *
 * @param name Identifier used to derive the `<name>_start` /
 *             `<name>_end` symbols.
 * @param path Path to the binary file to `.incbin`.
 */
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

/**
 * @brief Pads the CHR ROM section with @p count copies of @p val.
 * @param count Number of bytes to emit.
 * @param val   Byte value to fill with.
 */
#define CHARACTER_ROM_PAD(count, val)          \
__asm__(                                       \
_CHR_PUSH                                      \
".fill " #count ", 1, " #val "\n"              \
_CHR_POP                                       \
)

#ifndef TARGET_NES
/** @brief OAM coordinate type — 16-bit on desktop to allow off-screen sprites. */
typedef uint16_t oam_t;
#else
/** @brief OAM coordinate type — 8-bit on NES to match hardware OAM layout. */
typedef uint8_t oam_t;
#endif

/**
 * @brief A single sprite in the object attribute memory layout.
 */
struct sprite_t {
  oam_t y;            /**< Y coordinate of the top-left corner. */
  uint8_t tile;       /**< Pattern table tile index. */
  uint8_t attributes; /**< Palette select, priority, flip flags. */
  oam_t x;            /**< X coordinate of the top-left corner. */
};

#ifdef TARGET_NES
  /** @brief OAM buffer on NES — fixed 64-sprite array. */
  typedef struct sprite_t oamBuffer_t[64];
  /** @brief Number of sprites in the OAM buffer (NES hardware limit). */
  #define sOAM 64
  /** @brief Macro pointing at the raw OAM sprite array. */
  #define OAM_BUFFER oamBuffer
#else
  /** @brief OAM buffer on desktop — growable dynamic array. */
  typedef struct {
      struct sprite_t* data; /**< Sprite array. */
      size_t           count;/**< Sprites currently used. */
      size_t           cap;  /**< Allocated capacity. */
  } oamBuffer_t;
  /** @brief Current sprite count on desktop (variable). */
  extern size_t sOAM;
  /** @brief Macro pointing at the underlying sprite array. */
#define OAM_BUFFER oamBuffer.data
#endif

/** @brief Global OAM buffer populated by application code each frame. */
extern oamBuffer_t oamBuffer;

/**
 * @brief Forces alignment of the current insertion point inside the
 *        CHR ROM section.
 *
 * Emits a `.balign` directive inside ::_CHR_PUSH / ::_CHR_POP so the
 * next bytes added to the section (via ::CHARACTER_ROM, etc.) start
 * at an @p addr -byte boundary. Required by the PPU / emulated PPU,
 * which maps CHR pages on fixed 8 KB boundaries.
 *
 * @param addr Alignment in bytes.
 */
#define CHARACTER_ROM_ALIGN(addr)                \
__asm__(                                         \
_CHR_PUSH                                        \
".balign " #addr "\n"                            \
_CHR_POP                                         \
)

/**
 * @brief Typed pointer to the start of a named CHR ROM blob.
 * @param name Identifier previously passed to ::CHARACTER_ROM.
 */
#define CHR(name)       ((const uint8_t *)(name##_start))

/**
 * @brief Size in bytes of a named CHR ROM blob.
 * @param name Identifier previously passed to ::CHARACTER_ROM.
 */
#define CHR_SIZE(name)  ((size_t)(name##_end - name##_start))

#ifndef TARGET_NES
  #if defined(_WIN32)
    extern const uint8_t _chr_rom[];
  #elif defined(__APPLE__)
    extern const uint8_t _chr_rom[] __asm("section$start$__DATA$chr_rom");
  #else
    extern const uint8_t _chr_rom[] __asm("__start_chr_rom");
  #endif
  /** @brief Base pointer to the merged CHR ROM section (desktop). */
  #define CHR_ROM ((const uint8_t *)_chr_rom)
#endif

/**
 * @brief PPU register addresses.
 */
enum PPU {
    PPUCTRL     = 0x2000, /**< Base nametable, VRAM increment, NMI enable. */
    PPUMASK     = 0x2001, /**< Rendering enable and color emphasis. */
    PPUSTATUS   = 0x2002, /**< VBlank, sprite 0, sprite-overflow flags. */
    OAMADDR     = 0x2003, /**< OAM write address. */
    OAMDATA     = 0x2004, /**< OAM read/write data port. */
    PPUSCROLL   = 0x2005, /**< Fine X/Y scroll write. */
    PPUADDR     = 0x2006, /**< VRAM address write. */
    PPUDATA     = 0x2007, /**< VRAM data port. */

    OAMDMA      = 0x4014  /**< OAM DMA transfer trigger. */
};

/**
 * @brief Bit flags for ::PPUCTRL.
 */
enum CTRL {
    GEN_NMI     = 0x80, /**< Generate NMI on VBlank. */
    POLARITY    = 0x04, /**< VRAM address auto-increment direction (horizontal/vertical). */
    BG_ADDR     = 0x10, /**< Background pattern table at \$1000 (otherwise \$0000). */
    SPRITE_ADDR = 0x08  /**< Sprite pattern table at \$1000 (otherwise \$0000). */
};


/**
 * @brief Bit flags for ::PPUMASK.
 */
enum MASK {
    BG             = 0x08, /**< Show background. */
    SPRITE         = 0x10, /**< Show sprites. */
    BG_L           = 0x0a, /**< Show background in the left 8 pixels of the screen. */
    SPRITE_L       = 0x14, /**< Show sprites in the left 8 pixels of the screen. */
    RED            = 0x20, /**< Emphasise red. */
    GREEN          = 0x40, /**< Emphasise green. */
    BLUE           = 0x80, /**< Emphasise blue. */
};

/**
 * @brief Palette selector values for attribute-table writes.
 *
 * Each value encodes a palette index in the upper bits of an
 * attribute byte: BG_N selects background palette N; SPRITE_N selects
 * sprite palette N.
 */
enum PALETTE {
    BG_0          = 0 << 2, /**< Background palette 0. */
    BG_1          = 1 << 2, /**< Background palette 1. */
    BG_2          = 2 << 2, /**< Background palette 2. */
    BG_3          = 3 << 2, /**< Background palette 3. */
    SPRITE_0      = 4 << 2, /**< Sprite palette 0. */
    SPRITE_1      = 5 << 2, /**< Sprite palette 1. */
    SPRITE_2      = 6 << 2, /**< Sprite palette 2. */
    SPRITE_3      = 7 << 2, /**< Sprite palette 3. */
};

/**
 * @brief Blocks until the renderer has presented the current frame.
 *
 * On NES builds this waits for VBlank via ::PPUSTATUS; on desktop it
 * waits on the SDL3 present fence.
 */
void WaitForPresent();

/**
 * @brief Enables rendering by writing to ::PPUCTRL and ::PPUMASK.
 * @param ppuCtrl_ Value to latch into ::PPUCTRL (see ::CTRL flags).
 * @param ppuMask_ Value to latch into ::PPUMASK (see ::MASK flags).
 */
void EnableRendering(uint8_t ppuCtrl_, uint8_t ppuMask_);

#ifdef TARGET_NES
/** @brief Viewport width in tiles (NES: fixed 32). */
#define VIEWPORT_TX  32
/** @brief Viewport height in tiles (NES: fixed 30). */
#define VIEWPORT_TY  30
#else
/** @brief Current desktop display mode (window + refresh info). */
extern const SDL_DisplayMode* mode;
/** @brief Integer upscaling factor applied to the NES virtual framebuffer. */
extern uint8_t scale;

/** @brief Viewport width in tiles, computed from the desktop window size. */
#define VIEWPORT_TX  \
  (((mode->w / scale) >> 3) & ~3u)
/** @brief Viewport height in tiles, computed from the desktop window size. */
#define VIEWPORT_TY  \
  ((mode->h / scale) >> 3)
#endif

/** @brief Viewport width in pixels (tiles * 8). */
#define VIEWPORT_PX  (VIEWPORT_TX << 3)
/** @brief Viewport height in pixels (tiles * 8). */
#define VIEWPORT_PY  (VIEWPORT_TY << 3)


#ifndef TARGET_NES
/** @brief Desktop shadow of the PPU VRAM. */
extern uint8_t* VideoRAM;
/** @brief Desktop shadow of the PPU palette RAM. */
extern uint8_t* paletteRAM;
/** @brief Current horizontal scroll (pixels). */
extern uint16_t xScroll;
/** @brief Current vertical scroll (pixels). */
extern uint16_t yScroll;
#endif

#ifdef TARGET_NES
/**
 * @brief Platform-specific scroll encoding (NES: packed PPU registers).
 *
 * Layout:
 * - `data[0]` = ::PPUCTRL byte, nametable select already merged in.
 * - `data[1]` = fine X scroll (`px & 0xFF`).
 * - `data[2]` = fine Y scroll (`py % 240` after nametable wrap).
 */
typedef struct { uint8_t data[3]; } scroll_t;
#else
/** @brief 2-D unsigned 16-bit vector used on desktop. */
typedef struct { uint16_t x; uint16_t y; } vec2u16;
/** @brief Platform-specific scroll encoding (desktop: plain XY pixels). */
typedef vec2u16 scroll_t;
#endif

/**
 * @brief Converts a pixel position into the platform ::scroll_t representation.
 *
 * NES builds encode nametable select plus fine X/Y into the 3-byte
 * PPU format. Desktop builds just store the pixel coordinates.
 *
 * @param px Horizontal pixel position.
 * @param py Vertical pixel position.
 * @return   Encoded scroll value.
 */
scroll_t CartesianToScroll(uint16_t px, uint16_t py);

#ifdef TARGET_NES
  /**
   * @brief Latches a ::scroll_t into the PPU scroll registers (NES).
   * @param s A ::scroll_t produced by ::CartesianToScroll.
   */
  #define WRITE_SCROLL(s) do { \
      (*(volatile uint8_t*)PPUCTRL)   = (s).data[0]; \
      (*(volatile uint8_t*)PPUSCROLL) = (s).data[1]; \
      (*(volatile uint8_t*)PPUSCROLL) = (s).data[2]; \
  } while(0)
#else
  /** @brief Writes a ::scroll_t into the desktop scroll globals. */
  #define WRITE_SCROLL(s) do { xScroll = (s).x; yScroll = (s).y; } while(0)
#endif

/**
 * @brief Adds a signed delta to the current scroll.
 * @param x Horizontal delta, in pixels.
 * @param y Vertical delta, in pixels.
 */
void DeltaScroll(int8_t x, int8_t y);

/**
 * @brief Sets the absolute scroll of the screen.
 * @param x New horizontal scroll, in pixels.
 * @param y New vertical scroll, in pixels.
 */
void SetScroll(uint16_t x, uint16_t y);

/**
 * @brief Writes an array of bytes into nametable memory with a stride.
 *
 * Copies @p source byte-by-byte starting at the tile position
 * corresponding to (@p x, @p y). @p polarity selects horizontal
 * (stride 1) or vertical (stride 32) writes, matching ::CTRL::POLARITY.
 *
 * @param x        Tile X position (pixels / 8).
 * @param y        Tile Y position (pixels / 8).
 * @param source   Source buffer to push into PPU video RAM.
 * @param sBuffer  Size of @p source in bytes.
 * @param polarity Non-zero for vertical writes, zero for horizontal.
 */
void WriteBufferToVideoMemory(
  const uint16_t x, const uint16_t y, const uint8_t* source, uint8_t sBuffer, uint8_t polarity
);

/**
 * @brief Writes a single byte into nametable memory.
 * @param x     Tile X position.
 * @param y     Tile Y position.
 * @param value Byte value to write.
 */
void WriteSingleToVideoMemory(const uint16_t x, const uint16_t y, uint8_t value);

/**
 * @brief Flushes pending nametable and attribute-table writes to the PPU.
 * @param nt Nametable index to flush.
 * @param at Attribute-table index to flush.
 */
void FlushVideoRAM(const uint8_t nt, const uint8_t at);

/**
 * @brief Writes an array of bytes into palette memory.
 * @param offset   Palette RAM offset to start writing at.
 * @param source   Source buffer of palette indices.
 * @param sBuffer  Size of @p source in bytes.
 */
void WriteBufferToPaletteMemory(const uint8_t offset, const uint8_t* source, uint8_t sBuffer);

/**
 * @brief Writes a single byte into palette memory.
 * @param offset Palette RAM offset.
 * @param value  Palette index to store.
 */
void WriteSingleToPaletteMemory(const uint8_t offset, uint8_t value);

/**
 * @brief Writes bytes produced by a provider callback into nametable memory.
 *
 * Equivalent to ::WriteBufferToVideoMemory but sources each byte from
 * `fn(i)` instead of a preallocated buffer — useful for patterns and
 * procedurally generated rows.
 *
 * @param x        Tile X position.
 * @param y        Tile Y position.
 * @param fn       Provider returning the byte to write for iteration `i`.
 * @param amt      Number of iterations.
 * @param polarity Non-zero for vertical writes, zero for horizontal.
 */
void WriteProviderToVideoMemory(uint16_t x, const uint16_t y, uint8_t (*fn)(uint16_t), uint8_t amt, uint8_t polarity);

/**
 * @brief Converts a pixel position into a PPU VRAM address.
 * @param x Tile X position.
 * @param y Tile Y position.
 * @return  Absolute PPU address of the corresponding nametable byte.
 */
uint16_t CartesianToAddress(uint16_t x, uint16_t y);

/**
 * @brief Writes an array of bytes into the attribute table with a stride.
 *
 * Same layout as ::WriteBufferToVideoMemory but targets attribute
 * memory instead of the nametable.
 *
 * @param x        Tile X position.
 * @param y        Tile Y position.
 * @param source   Source buffer of attribute bytes.
 * @param sBuffer  Size of @p source in bytes.
 * @param polarity Non-zero for vertical, zero for horizontal.
 */
void WriteBufferToAttributeMemory(
  const uint16_t x, const uint16_t y, const uint8_t* source, const uint8_t sBuffer, uint8_t polarity
);

/**
 * @brief Writes a single byte into attribute memory.
 * @param x     Tile X position.
 * @param y     Tile Y position.
 * @param value Attribute byte (palette + flip flags).
 */
void WriteSingleToAttributeMemory(const uint16_t x, const uint16_t y, uint8_t value);

/**
 * @brief Uploads ::oamBuffer to the PPU via OAM DMA.
 *
 * Call once per frame, after the application has populated sprites
 * in ::oamBuffer.
 */
void RefreshSprites(void);

/**
 * @brief Sets the color emphasis bits in ::PPUMASK (bits 5-7).
 * @param priority OR of ::MASK bits (::RED, ::GREEN, ::BLUE).
 */
void SetColorPriority(uint8_t priority);

#ifdef TARGET_NES
/** @brief Sprite-zero-hit handler — NES variant (parameterless). */
typedef void (*spriteZeroHandler_t)(void);
#else
/**
 * @brief Sprite-zero-hit handler — desktop variant.
 *
 * The desktop renderer needs to know where the sprite-zero test
 * should trip, so the handler is bundled with its trigger pixel.
 */
typedef struct {
  void (*method)(void); /**< Callback fired on sprite-zero hit. */
  uint16_t px;          /**< Pixel X at which to fire. */
  uint16_t py;          /**< Pixel Y at which to fire. */
} spriteZeroHandler_t;

/**
 * @brief Registers a sprite-zero-hit handler (desktop only).
 * @param px Pixel X at which to fire.
 * @param py Pixel Y at which to fire.
 * @param fn Callback to invoke when the sprite-zero test trips.
 */
void SetSpriteZeroHandler(uint16_t px, uint16_t py, void (*fn)(void));
/** @brief Convenience wrapper around ::SetSpriteZeroHandler. */
#define SET_SPRITE_ZERO_HANDLER(px, py, fn) SetSpriteZeroHandler(px, py, fn)
#endif

/**
 * @brief Spins until the beam crosses (@p px, @p py), then invokes @p fn.
 *
 * The NES implementation busy-waits on the sprite-zero hit flag in
 * ::PPUSTATUS; the desktop implementation schedules @p fn via the
 * renderer. In both cases @p latch is set to a non-zero value when
 * the handler has run, so frame code can detect completion.
 *
 * @param px    Pixel X trigger.
 * @param py    Pixel Y trigger.
 * @param fn    Callback to fire.
 * @param latch Flag written non-zero when @p fn has completed.
 */
void WaitThenReactToSpriteZero(uint16_t px, uint16_t py, void (*fn)(void), atomic uint8_t* latch);

#ifdef TARGET_NES
/** @brief Shadow copy of ::PPUCTRL, updated atomically. */
extern atomic uint8_t SPPUCTRL;
/** @brief Shadow copy of ::PPUMASK, updated atomically. */
extern atomic uint8_t SPPUMASK;

/**
 * @brief Safe-VRAM-access block (NES).
 *
 * Disables rendering by zeroing ::PPUMASK, runs the body once, then
 * restores the shadowed mask. Use it to bracket VRAM writes that must
 * not happen mid-frame:
 *
 * @code
 *   VRAM {
 *     WriteSingleToVideoMemory(0, 0, 0x20);
 *   }
 * @endcode
 */
#define VRAM                                \
for (                                       \
uint8_t i = (POKE(PPUMASK, 0), 0);\
__builtin_expect(i < 1, 1);                 \
i++, POKE(PPUMASK, SPPUMASK))

#else
/**
 * @brief Safe-VRAM-access block (desktop).
 *
 * No-op bracket that the desktop renderer synchronises internally;
 * source compatible with the NES ::VRAM macro.
 */
#define VRAM \
if (1)

#endif

#endif
