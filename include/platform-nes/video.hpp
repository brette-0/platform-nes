/**
 * @file video.hpp
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

#include <intsh>
using namespace br0::intsh;
#include <stddef.h>

#include "technology.hpp"

#ifndef TARGET_NES
#include <SDL3/SDL_video.h>
#endif

/** @brief Base PPU address of the pattern tables (\$0000 / \$1000). */
extern const u16 PatternTables;

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
ASM_LINKAGE const u8 name##_start[];        \
ASM_LINKAGE const u8 name##_end[];

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
typedef u16 oam_t;
#else
/** @brief OAM coordinate type — 8-bit on NES to match hardware OAM layout. */
typedef u8 oam_t;
#endif

/**
 * @brief A single sprite in the object attribute memory layout.
 */
struct sprite_t {
  oam_t y;            /**< Y coordinate of the top-left corner. */
  u8 tile;         /**< Pattern table tile index. */
  u8 attributes;   /**< Palette select, priority, flip flags. */
  oam_t x;            /**< X coordinate of the top-left corner. */
};

/**
 * @brief Number of sprites in an OAM region.
 *
 * An OAM buffer is simply a pointer to `OAM_SPRITES` consecutive
 * ::sprite_t records — `OAM_SPRITES * sizeof(struct sprite_t)` bytes —
 * on both NES and desktop. Application code owns the storage and passes
 * the pointer to every OAM call, so several independent buffers may
 * coexist.
 */
#define OAM_SPRITES 64

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
#define CHR(name)       ((const u8 *)(name##_start))

/**
 * @brief Size in bytes of a named CHR ROM blob.
 * @param name Identifier previously passed to ::CHARACTER_ROM.
 */
#define CHR_SIZE(name)  ((size_t)(name##_end - name##_start))

#ifndef TARGET_NES
  #if defined(_WIN32)
    extern const u8 _chr_rom[];
  #elif defined(__APPLE__)
    extern const u8 _chr_rom[] __asm("section$start$__DATA$chr_rom");
  #else
    extern const u8 _chr_rom[] __asm("__start_chr_rom");
  #endif
  /** @brief Base pointer to the merged CHR ROM section (desktop). */
  #define CHR_ROM ((const u8 *)_chr_rom)
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
void EnableRendering(u8 ppuCtrl_, u8 ppuMask_);

#ifdef TARGET_NES
/** @brief Viewport width in tiles (NES: fixed 32). */
#define VIEWPORT_TX  32
/** @brief Viewport height in tiles (NES: fixed 30). */
#define VIEWPORT_TY  30
#else
/** @brief Current desktop display mode (window + refresh info). */
extern const SDL_DisplayMode* mode;
/** @brief Integer upscaling factor applied to the NES virtual framebuffer. */
extern u8 scale;

#ifdef LANDSCAPE
/** @brief Viewport width in tiles — flexible on landscape, derived from window width. */
#define VIEWPORT_TX  \
  (((mode->w / scale) >> 3) & ~3u)
/** @brief Viewport height in tiles — pinned to 30, matching the axis `scale` was chosen for. */
#define VIEWPORT_TY  30
#else
/** @brief Viewport width in tiles — pinned to 32, matching the axis `scale` was chosen for. */
#define VIEWPORT_TX  32
/** @brief Viewport height in tiles — flexible on portrait, derived from window height. */
#define VIEWPORT_TY  \
  ((mode->h / scale) >> 3)
#endif
#endif

/** @brief Viewport width in pixels (tiles * 8). */
#define VIEWPORT_PX  (VIEWPORT_TX << 3)
/** @brief Viewport height in pixels (tiles * 8). */
#define VIEWPORT_PY  (VIEWPORT_TY << 3)


#ifndef TARGET_NES
/** @brief Desktop shadow of the PPU VRAM. */
extern u8* VideoRAM;
/** @brief Desktop shadow of the PPU palette RAM. */
extern u8* paletteRAM;
/** @brief Current horizontal scroll (pixels). */
extern u16 xScroll;
/** @brief Current vertical scroll (pixels). */
extern u16 yScroll;
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
typedef struct { u8 data[3]; } scroll_t;
#else
/** @brief 2-D unsigned 16-bit vector used on desktop. */
typedef struct { u16 x; u16 y; } vec2u16;
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
scroll_t CartesianToScroll(u16 px, u16 py);

#ifdef TARGET_NES
  /**
   * @brief Latches a ::scroll_t into the PPU scroll registers (NES).
   * @param s A ::scroll_t produced by ::CartesianToScroll.
   */
  #define WRITE_SCROLL(s) do { \
      (*(volatile u8*)PPUCTRL)   = (s).data[0]; \
      (*(volatile u8*)PPUSCROLL) = (s).data[1]; \
      (*(volatile u8*)PPUSCROLL) = (s).data[2]; \
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
void DeltaScroll(i8 x, i8 y);

/**
 * @brief Sets the absolute scroll of the screen.
 * @param x New horizontal scroll, in pixels.
 * @param y New vertical scroll, in pixels.
 */
void SetScroll(u16 x, u16 y);

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
  const u16 x, const u16 y, const u8* source, u8 sBuffer, u8 polarity
);

/**
 * @brief Writes a single byte into nametable memory.
 * @param x     Tile X position.
 * @param y     Tile Y position.
 * @param value Byte value to write.
 */
void WriteSingleToVideoMemory(const u16 x, const u16 y, u8 value);

/**
 * @brief Flushes pending nametable and attribute-table writes to the PPU.
 * @param nt Nametable index to flush.
 * @param at Attribute-table index to flush.
 */
void FlushVideoRAM(const u8 nt, const u8 at);

/**
 * @brief Writes an array of bytes into palette memory.
 * @param offset   Palette RAM offset to start writing at.
 * @param source   Source buffer of palette indices.
 * @param sBuffer  Size of @p source in bytes.
 */
void WriteBufferToPaletteMemory(const u8 offset, const u8* source, u8 sBuffer);

/**
 * @brief Writes a single byte into palette memory.
 * @param offset Palette RAM offset.
 * @param value  Palette index to store.
 */
void WriteSingleToPaletteMemory(const u8 offset, u8 value);

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
void WriteProviderToVideoMemory(u16 x, const u16 y, u8 (*fn)(u16),
  u8 amt, u8 polarity);

/**
 * @brief Converts a pixel position into a PPU VRAM address.
 * @param x Tile X position.
 * @param y Tile Y position.
 * @return  Absolute PPU address of the corresponding nametable byte.
 */
u16 CartesianToAddress(u16 x, u16 y);

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
  const u16 x, const u16 y, const u8* source,
  const u8 sBuffer, u8 polarity
);

/**
 * @brief Writes a single byte into attribute memory.
 * @param x     Tile X position.
 * @param y     Tile Y position.
 * @param value Attribute byte (palette + flip flags).
 */
void WriteSingleToAttributeMemory(const u16 x, const u16 y, u8 value);

/**
 * @brief Uploads an OAM buffer to the PPU via OAM DMA.
 *
 * Call once per frame, after the application has populated sprites in
 * @p oam. On NES this triggers the hardware OAM DMA from the buffer's
 * page (so @p oam must be 256-byte aligned); on desktop it freezes a
 * snapshot the renderer reads for the next frame.
 *
 * @param oam Pointer to the ::OAM_SPRITES-sprite buffer to upload.
 */
void RefreshSprites(const sprite_t *oam);

/**
 * @brief Sets the color emphasis bits in ::PPUMASK (bits 5-7).
 * @param priority OR of ::MASK bits (::RED, ::GREEN, ::BLUE).
 */
void SetColorPriority(u8 priority);

#ifdef TARGET_NES
/** @brief Sprite-zero-hit handler — NES variant (parameterless). */
typedef void (*spriteZeroHandler_t)();
#else
/**
 * @brief Sprite-zero-hit handler — desktop variant.
 *
 * The desktop renderer needs to know where the sprite-zero test
 * should trip, so the handler is bundled with its trigger pixel.
 */
typedef struct {
  void (*method)(void); /**< Callback fired on sprite-zero hit. */
  u16 px;          /**< Pixel X at which to fire. */
  u16 py;          /**< Pixel Y at which to fire. */
} spriteZeroHandler_t;

/**
 * @brief Registers a sprite-zero-hit handler (desktop only).
 * @param px Pixel X at which to fire.
 * @param py Pixel Y at which to fire.
 * @param fn Callback to invoke when the sprite-zero test trips.
 */
void SetSpriteZeroHandler(u16 px, u16 py, void (*fn)(void));
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
void WaitThenReactToSpriteZero(u16 px, u16 py, void (*fn)(), atomic u8* latch);

#ifdef TARGET_NES
/** @brief Shadow copy of ::PPUCTRL, updated atomically. */
extern atomic u8 SPPUCTRL;
/** @brief Shadow copy of ::PPUMASK, updated atomically. */
extern atomic u8 SPPUMASK;

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
u8 i = (POKE(PPUMASK, 0), 0);\
__builtin_expect(i < 1, 1);                 \
POKE(PPUMASK, SPPUMASK), i++)

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

#define SPRITE_STRIDE  sizeof(struct sprite_t)
#define SPRITE_SLOT(i) ((i) * SPRITE_STRIDE)

/**
 * @brief Writes one ::sprite_t field across @p count consecutive sprites.
 *
 * Hardware-specific OAM write: the sprite stride is baked in, and the
 * field's byte offset and width are derived from @p member at compile
 * time. On NES every field is one byte; on desktop ::oam_t coordinate
 * fields are two bytes and are written in full, so off-screen sprite
 * positions are preserved. The provider returns ::oam_t.
 *
 * @param oam    OAM buffer to write into.
 * @param slot   First sprite index to write.
 * @param member A field of ::sprite_t (e.g. `x`, `y`, `tile`).
 * @param fn     Provider returning the value for iteration `i`.
 * @param count  Number of sprites to write.
 */
#define PopulateOAMFromProvider(oam, slot, member, fn, count)   \
    OAMFromProvider((oam), (slot),                             \
                    offsetof(struct sprite_t, member),         \
                    sizeof(((struct sprite_t *)0)->member),    \
                    (fn), (count))

/**
 * @brief Copies one ::sprite_t field into @p count consecutive sprites.
 *
 * The buffer counterpart of ::PopulateOAMFromProvider: instead of a
 * callback, each value is read from @p src, which is laid out as
 * ::sprite_t records (e.g. a metasprite table). The @p member field is
 * copied from `src[i]` to `oam[slot + i]`; the sprite stride, the
 * field offset and its width are all handled internally.
 *
 * @param oam    OAM buffer to write into.
 * @param slot   First sprite index to write.
 * @param member A field of ::sprite_t (e.g. `tile`, `attributes`, `x`).
 * @param src    Source laid out as ::sprite_t records.
 * @param count  Number of sprites to write.
 */
#define PopulateOAMFromBuffer(oam, slot, member, src, count)    \
    OAMFromBuffer((oam), (slot),                               \
                  offsetof(struct sprite_t, member),           \
                  sizeof(((struct sprite_t *)0)->member),      \
                  (const u8 *)(src), (count))

void OAMFromProvider(sprite_t *oam, u8 slot, u16 off,
                     u8 width, oam_t (*fn)(u16), u16 count);
void OAMFromBuffer(sprite_t *oam, u8 slot, u16 off,
                   u8 width, const u8 *src, u16 count);


void StreamFromVideoMemory(u16 offset, atomic u8* target, u8 size);

#endif
