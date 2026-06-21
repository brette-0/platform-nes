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
#include <cstddef>
#include <array>

#include "technology.hpp"

// The SDL desktop backend pulls in SDL3's display-mode types here. The libogc
// (GameCube/Wii), 3DS (citro2d), Switch (libnx framebuffer), Wii U and Nintendo
// DS backends are emulated-PPU builds like SDL but present through console
// graphics, so they must NOT see any SDL3 headers. (The Wii U backend DOES use
// SDL -- but SDL2, from the devkitPro Wii U portlib -- which it includes itself
// in src/wiiu; it must not pull in SDL3. The DS and GBA backends drive the
// libnds/libgba 2D hardware directly, with no SDL at all.) All are "non-NES", so
// the SDL3-only includes/globals are gated on
// (!TARGET_NES && !OGC && !CTR && !NX && !WIIU && !NDS && !GBA).
#if !defined(TARGET_NES) && !defined(TARGET_OGC) && !defined(TARGET_CTR) && !defined(TARGET_NX) && !defined(TARGET_WIIU) && !defined(TARGET_NDS) && !defined(TARGET_GBA)
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
 * @brief Forward-declares the `<name>_start` / `<name>_end` symbols of a CHR
 *        ROM blob defined elsewhere.
 *
 * Use this in a header to reference a blob from a different translation unit:
 * the blob is *defined* once by ::CHARACTER_ROM in a single `.cpp`, and any TU
 * that includes the header may then use ::CHR and ::CHR_SIZE on @p name. Do not
 * place ::CHARACTER_ROM itself in a header -- its `.incbin` would embed the
 * bytes (and redefine the symbols) in every TU that includes it.
 *
 * @param name Identifier matching the one passed to ::CHARACTER_ROM.
 */
#define EXTERN_CHARACTER_ROM(name)               \
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

/* ------------------------------------------------------------------------ *
 *  Symbolic CHR tiles (#embed)                                             *
 *                                                                          *
 *  Unlike ::CHARACTER_ROM (which `.incbin`s a blob and exposes only its    *
 *  link-time start/end), these macros pull each `.chr` file in via         *
 *  `#embed`, so the file's size is a *compile-time* constant. From that we  *
 *  derive, with zero runtime cost, a `<name>_tile` constant: the blob's    *
 *  base CHR-tile index (byte offset / 16). Tile ids are never hand-        *
 *  assigned and are usable inside `constexpr` tables (e.g. metatiles).     *
 *                                                                          *
 *  Authoring (one ordered list, e.g. in a `chr.hpp` shared header):        *
 *                                                                          *
 *      CHARACTER_ROM_BEGIN(chrSprite0)                                     *
 *      #embed "../../chr/sprites/sprite0.chr"                              *
 *      CHARACTER_ROM_END(chrSprite0, CHR_ORIGIN);   // first: chains origin *
 *                                                                          *
 *      CHARACTER_ROM_BEGIN(chrBush)                                        *
 *      #embed "../../chr/tiles/static/bush.chr"                            *
 *      CHARACTER_ROM_END(chrBush, chrSprite0);      // prev = line above   *
 *      ...                                                                 *
 *      CHARACTER_ROM_BEGIN(chrCoin)                                        *
 *      #embed "../../chr/tiles/dynamic/coin.chr"                           *
 *      CHARACTER_ROM_END_FINAL(chrCoin, chrBush, 0x2000); // last: + image *
 *                                                                          *
 *  Just `#include` the list wherever you use tile ids. The FINAL blob's    *
 *  CHARACTER_ROM_END_FINAL emits the single padded CHR image as an `inline`*
 *  object, so the linker folds it to one copy no matter how many TUs       *
 *  include the list -- no dedicated emit TU and no magic define. (One TU is*
 *  cheapest; extra includers are merely safe.) `#embed` cannot be produced *
 *  by a macro, so the `#embed` line is written literally between BEGIN/END.*
 *                                                                          *
 *  Byte order (hence every `_tile`) follows the `prev` chain, not source   *
 *  position, so ids and placement can never disagree.                      *
 * ------------------------------------------------------------------------ */

/** @brief Platform CHR section name, for `[[gnu::section]]` placement. */
#ifdef TARGET_NES
  #define _CHR_SECTION ".chr_rom"
#elif defined(_WIN32)
  #define _CHR_SECTION "chr_rom$m"
#elif defined(__APPLE__)
  #define _CHR_SECTION "__DATA,chr_rom"
#else
  #define _CHR_SECTION "chr_rom"
#endif

namespace nes_chr {
  /** @brief Copy a `#embed` staging buffer into a sized, constexpr-friendly
   *  array of bytes. The source is `int` because the two compilers disagree on
   *  the element type a `#embed` expands to: GCC yields ints in [0,255], while
   *  Clang yields char-typed values (0xFF -> -1 on a signed-char target). No
   *  8-bit type holds that combined [-128,255] range without a braced-init
   *  narrowing error, so the staging buffer is `int`; the byte value is recovered
   *  here with an explicit cast and every downstream type (accum, the CHR image,
   *  the `_tile` ids) stays `u8`. The buffer is internal-linkage constexpr,
   *  consumed purely at compile time, so its width carries no runtime cost. */
  template <size_t N>
  constexpr std::array<u8, N> make(const int (&a)[N]) {
    std::array<u8, N> r{};
    for (size_t i = 0; i < N; ++i) r[i] = static_cast<u8>(a[i]);
    return r;
  }
  /** @brief Concatenate two byte blocks at compile time (order preserved). */
  template <size_t A, size_t B>
  constexpr std::array<u8, A + B> cat(const std::array<u8, A>& x,
                                      const std::array<u8, B>& y) {
    std::array<u8, A + B> r{};
    for (size_t i = 0; i < A; ++i) r[i]     = x[i];
    for (size_t i = 0; i < B; ++i) r[A + i] = y[i];
    return r;
  }
  /** @brief Right-pad a byte block with zeros to a fixed CHR-image size. */
  template <size_t Total, size_t N>
  constexpr std::array<u8, Total> pad(const std::array<u8, N>& s) {
    static_assert(N <= Total, "CHR tile data exceeds the CHR ROM image size");
    std::array<u8, Total> r{};
    for (size_t i = 0; i < N; ++i) r[i] = s[i];
    return r;
  }
}   // namespace nes_chr

/** @brief Chain anchor: the first blob's `prev`. Resolves to tile 0. */
constexpr u8 CHR_ORIGIN_tile   = 0;
constexpr u8 CHR_ORIGIN_ntiles = 0;
constexpr std::array<u8, 0> CHR_ORIGIN_accum{};

/** @brief CHR tiles per 4 KB PPU pattern table ($1000 bytes / 16). The boundary
 *         between the sprite table ($0000) and the background table ($1000). */
constexpr unsigned CHR_TILES_PER_TABLE = 256;

// The running placement-concat feeds the final padded image. It is built in
// every TU that includes the list, but is internal-linkage constexpr (consumed
// purely at compile time), so it never reaches any object file.
#define _CHR_PLACE(name, prev)                                         \
  constexpr auto name##_accum =                                        \
      nes_chr::cat(prev##_accum, nes_chr::make(name##_raw));

/** @brief Opens a `#embed` CHR blob named @p name (followed by a literal
 *         `#embed "path"` line, then ::CHARACTER_ROM_END). */
#define CHARACTER_ROM_BEGIN(name) constexpr int name##_raw[] = {

/** @brief Closes a `#embed` CHR blob and derives `<name>_tile` /
 *         `<name>_ntiles`. @p prev is the blob declared immediately before
 *         (or ::CHR_ORIGIN for the first), giving the byte/id ordering. */
#define CHARACTER_ROM_END(name, prev)                                  \
  };                                                                   \
  constexpr u8 name##_tile   = (u8)(prev##_tile + prev##_ntiles);      \
  constexpr u8 name##_ntiles =                                         \
      (u8)(sizeof(name##_raw) / sizeof(name##_raw[0]) / 16);           \
  _CHR_PLACE(name, prev)

/**
 * @brief Insert zero-fill into the CHR chain so the *next* blob begins at tile
 *        index @p to_tile -- e.g. align background graphics to the $1000
 *        pattern-table boundary while sprites stay in pattern table 0.
 *
 * The PPU addresses background and sprite graphics through two independent 4 KB
 * pattern tables ($0000 and $1000, ::CHR_TILES_PER_TABLE tiles each), selected
 * separately by PPUCTRL (::ppu::ctrl::BG_ADDR / ::ppu::ctrl::SPRITE_ADDR). A blob
 * physically placed at tile 256 lives at $1000 and is addressed as PT-relative
 * tile 0 once BG_ADDR selects $1000 -- which is exactly the value its
 * `<name>_tile` takes, because these ids are `u8` and wrap mod 256 at the table
 * boundary. So a pad here keeps every downstream `_tile` correct for free.
 *
 * Like every link in the chain it defines `<name>_tile`, `<name>_ntiles`, and
 * `<name>_accum`, so the following blob simply names this pad as its @p prev:
 *
 *      CHARACTER_ROM_END(chrMushletStanding, chrWand);   // last sprite
 *      CHARACTER_ROM_PAD_TO(chrBgGap, chrMushletStanding, CHR_TILES_PER_TABLE);
 *      CHARACTER_ROM_BEGIN(chrBush)                       // first BG tile
 *      #embed "..."
 *      CHARACTER_ROM_END(chrBush, chrBgGap);
 *
 * @param name    Identifier for this padding link (any unused name).
 * @param prev    The blob declared immediately before.
 * @param to_tile Tile index the next blob should start at. Must be within one
 *                table (<= 256 tiles) of the current position.
 */
#define CHARACTER_ROM_PAD_TO(name, prev, to_tile)                      \
  constexpr u8  name##_tile      = (u8)(prev##_tile + prev##_ntiles);  \
  constexpr int name##_pad_tiles = (int)(to_tile)                      \
                                 - (int)(prev##_tile + prev##_ntiles); \
  static_assert(name##_pad_tiles >= 0,                                 \
      "CHARACTER_ROM_PAD: data before the pad already passes to_tile");\
  static_assert(name##_pad_tiles <= (int)CHR_TILES_PER_TABLE,          \
      "CHARACTER_ROM_PAD: gap exceeds one pattern table");             \
  constexpr u8  name##_ntiles    = (u8)name##_pad_tiles;               \
  constexpr auto name##_accum    =                                     \
      nes_chr::cat(prev##_accum,                                       \
                   std::array<u8, (size_t)name##_pad_tiles * 16>{});

/* Materialize the padded CHR ROM image from a blob's accumulation (the whole
 * chain) and place it in the CHR section. Internal helper for
 * ::CHARACTER_ROM_END_FINAL -- not for direct use.
 *
 * `inline` gives the image external linkage with COMDAT (linkonce_odr) folding:
 * any number of TUs may include the blob list, yet the linker keeps exactly one
 * copy of the byte-identical image -- so there is no dedicated emit TU and no
 * magic define. The object name is fixed (never supplied by the caller), so a
 * second CHARACTER_ROM_END_FINAL in the SAME TU is still a redefinition error,
 * enforcing "one cartridge, one CHR ROM image". (Across TUs the lists are the
 * same header, hence ODR-identical, so folding is well-defined.) */
#define _CHR_EMIT_IMAGE(name, total_bytes)                             \
  [[gnu::section(_CHR_SECTION), gnu::used, gnu::retain]]               \
  inline constexpr auto chr_rom_image = nes_chr::pad<total_bytes>(name##_accum)

/**
 * @brief Close the FINAL blob in the chain and, in the emitting TU, emit the
 *        whole cartridge's CHR ROM image.
 *
 * Use this in place of ::CHARACTER_ROM_END for the last blob. It does
 * everything ::CHARACTER_ROM_END does, then materializes the single padded CHR
 * ROM image from this blob's accumulation (i.e. the entire chain) and places it
 * in the CHR section.
 *
 * @p total_bytes is the size of the *entire* CHR ROM -- not the 8 KB the PPU
 * sees at once. The mapper (e.g. MMC3) banks windows of this image into the
 * PPU's fixed $0000-$1FFF aperture, so it may be anything from 8 KB up to the
 * mapper's maximum (e.g. 256 KB). There is **no default**: omit it and the
 * preprocessor rejects the call.
 *
 * The image object is named internally; the caller neither passes nor sees a
 * name for it. Because that name is fixed, a second CHARACTER_ROM_END_FINAL in
 * the emitting TU is a redefinition error -- "exactly one CHR ROM image" is
 * enforced by the compiler, not by convention.
 *
 * @param name        This (final) blob's identifier.
 * @param prev        The blob declared immediately before (or ::CHR_ORIGIN).
 * @param total_bytes Total CHR ROM size in bytes (required; no default).
 */
#define CHARACTER_ROM_END_FINAL(name, prev, total_bytes)             \
  CHARACTER_ROM_END(name, prev)                                      \
  _CHR_EMIT_IMAGE(name, total_bytes)

#ifndef TARGET_NES
  #if defined(_WIN32)
    // C linkage so the symbol is the bare `_chr_rom` defined by the inline-asm
    // block in video.cpp. As a C++ variable it would take the MSVC-mangled
    // name and fail to resolve against that undecorated asm label at link time.
    extern "C" const u8 _chr_rom[];
  #elif defined(__APPLE__)
    extern const u8 _chr_rom[] __asm("section$start$__DATA$chr_rom");
  #else
    extern const u8 _chr_rom[] __asm("__start_chr_rom");
  #endif
  /** @brief Base pointer to the merged CHR ROM section (desktop). */
  #define CHR_ROM ((const u8 *)_chr_rom)
#endif

namespace oam {
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

    constexpr auto spriteStride =  sizeof(struct sprite_t);
    constexpr auto spriteSlot(const u16 slot) {return slot * spriteStride;}

    /**
     * @brief Compile-time selector for a ::sprite_t field.
     *
     * Replaces the old token-pasting macros: a tag carries the field's
     * byte @c offset and @c width so the OAM populate functions can
     * forward them to the backend with zero runtime cost. Pass one of the
     * predefined tags ::oam::y, ::oam::tile, ::oam::attributes, ::oam::x.
     */
    template<auto Off, auto Width>
    struct field_t {
        static constexpr u16 offset = Off;   /**< Byte offset within sprite_t. */
        static constexpr u8  width  = Width; /**< Field width in bytes.        */
    };

    /** @brief Field tag for ::sprite_t::y. */
    inline constexpr field_t<offsetof(sprite_t, y),          sizeof(sprite_t::y)>          y{};
    /** @brief Field tag for ::sprite_t::tile. */
    inline constexpr field_t<offsetof(sprite_t, tile),       sizeof(sprite_t::tile)>       tile{};
    /** @brief Field tag for ::sprite_t::attributes. */
    inline constexpr field_t<offsetof(sprite_t, attributes), sizeof(sprite_t::attributes)> attributes{};
    /** @brief Field tag for ::sprite_t::x. */
    inline constexpr field_t<offsetof(sprite_t, x),          sizeof(sprite_t::x)>          x{};

    void OAMFromProvider(sprite_t *oam, u8 slot, u16 off,
                 u8 width, oam_t (*fn)(u16), u16 count);
    void OAMFromBuffer(sprite_t *oam, u8 slot, u16 off,
                       u8 width, const u8 *src, u16 count);

    /**
     * @brief Writes one ::sprite_t field across @p count consecutive sprites.
     *
     * Hardware-specific OAM write: the sprite stride is baked in, and the
     * field's byte offset and width are carried by the @p field tag at
     * compile time. On NES every field is one byte; on desktop ::oam_t
     * coordinate fields are two bytes and are written in full, so off-screen
     * sprite positions are preserved. The provider returns ::oam_t.
     *
     * @param buf    OAM buffer to write into.
     * @param slot   First sprite index to write.
     * @param field  A ::oam::field_t tag (::oam::x, ::oam::y, ::oam::tile, ...).
     * @param fn     Provider returning the value for iteration `i`.
     * @param count  Number of sprites to write.
     */
    template<auto Off, auto Width>
    void PopulateFromProvider(sprite_t *buf, u8 slot,
                                        field_t<Off, Width> /*field*/,
                                        oam_t (*fn)(u16), u16 count) {
        OAMFromProvider(buf, slot, Off, Width, fn, count);
    }

    /**
     * @brief Copies one ::sprite_t field into @p count consecutive sprites.
     *
     * The buffer counterpart of ::oam::PopulateFromProvider: instead of a
     * callback, each value is read from @p src, which is laid out as
     * ::sprite_t records (e.g. a metasprite table). The field selected by the
     * @p field tag is copied from `src[i]` to `buf[slot + i]`; the sprite
     * stride, the field offset and its width are rest handled internally.
     *
     * @param buf    OAM buffer to write into.
     * @param slot   First sprite index to write.
     * @param field  A ::oam::field_t tag (::oam::x, ::oam::tile, ...).
     * @param src    Source laid out as ::sprite_t records.
     * @param count  Number of sprites to write.
     */
    template<auto Off, auto Width>
    void PopulateFromBuffer(sprite_t *buf, u8 slot,
                            field_t<Off, Width> /*field*/,
                            const void *src, u16 count) {
        OAMFromBuffer(buf, slot, Off, Width,
                      static_cast<const u8 *>(src), count);
    }
}

namespace ppu {
    namespace raw {
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
    }


    namespace ctrl {
        enum CTRL {
          GEN_NMI     = 0x80, /**< Generate NMI on VBlank. */
          POLARITY    = 0x04, /**< VRAM address auto-increment direction (horizontal/vertical). */
          BG_ADDR     = 0x10, /**< Background pattern table at \$1000 (otherwise \$0000). */
          SPRITE_ADDR = 0x08  /**< Sprite pattern table at \$1000 (otherwise \$0000). */
      };
    }

    namespace mask {
        enum MASK {
          BG             = 0x08, /**< Show background. */
          SPRITE         = 0x10, /**< Show sprites. */
          BG_L           = 0x0a, /**< Show background in the left 8 pixels of the screen. */
          SPRITE_L       = 0x14, /**< Show sprites in the left 8 pixels of the screen. */
          RED            = 0x20, /**< Emphasise red. */
          GREEN          = 0x40, /**< Emphasise green. */
          BLUE           = 0x80, /**< Emphasise blue. */
      };
    }

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
     * @brief Converts a pixel position into a PPU VRAM address.
     * @param x Tile X position.
     * @param y Tile Y position.
     * @return  Absolute PPU address of the corresponding nametable byte.
     */
    u16 CartesianToAddress(u16 x, u16 y);


    void StreamFromVideoMemory(u16 offset, atomic u8* target, u8 size);
}
#if !defined(TARGET_NES) && !defined(TARGET_OGC) && !defined(TARGET_CTR) && !defined(TARGET_NX) && !defined(TARGET_WIIU) && !defined(TARGET_NDS) && !defined(TARGET_GBA)
/** @brief Current desktop display mode (window + refresh info). SDL backend only. */
extern const SDL_DisplayMode* mode;
/** @brief Integer upscaling factor applied to the NES virtual framebuffer. SDL backend only. */
extern u8 scale;
#endif

namespace video {
#if defined(TARGET_NES) || defined(TARGET_OGC) || defined(TARGET_CTR)
    // NES, the libogc (GameCube/Wii) backend, and the 3DS (citro2d) backend all
    // present a fixed 32x30-tile NES frame. On NES this is the hardware PPU; on
    // OGC the emulated framebuffer is a fixed 256x240 surface that GX scales to
    // the TV; on 3DS the emulated 256x240 frame is uploaded as one texture and
    // citro2d scales it to the top screen. In every case the viewport is
    // constant (no window/display-mode dependency like the SDL backend has).
    /** @brief Viewport width in tiles (NES/OGC/CTR: fixed 32). */
    constexpr u16 viewport_tx() { return 32; }
    /** @brief Viewport height in tiles (NES/OGC/CTR: fixed 30). */
    constexpr u16 viewport_ty() { return 30; }
    /** @brief Viewport width in pixels (tiles * 8). */
    constexpr u16 viewport_px() { return viewport_tx() << 3; }
    /** @brief Viewport height in pixels (tiles * 8). */
    constexpr u16 viewport_py() { return viewport_ty() << 3; }
#elif defined(TARGET_NDS)
    // The Nintendo DS (and DSi) main engine is a fixed 256x192 panel -- the same
    // 32-tile NES width, but 6 tiles (48px) SHORTER than the NES's 240px height.
    // Per the project rule, the NES game is never compromised for another
    // platform: the DS simply shows a 256x192 WINDOW onto the same 256x240 world
    // the game renders. The backend (src/nds/video.cpp) maps the NES PPU onto the
    // DS's native 2D hardware (BG tilemap + hardware OBJ + per-scanline HBlank
    // scroll), so raster splits and sprite-0 still work; the bottom 48px of the
    // NES frame fall below the panel. This is the first target whose viewport is
    // shorter than the NES's 30 tiles, which is explicitly permitted: any code
    // reading video::viewport_ty() must not assume 30.
    /** @brief Viewport width in tiles (DS/DSi: fixed 32, full NES width). */
    constexpr u16 viewport_tx() { return 32; }
    /** @brief Viewport height in tiles (DS/DSi: 24 = the 192px panel, < NES 30). */
    constexpr u16 viewport_ty() { return 24; }
    /** @brief Viewport width in pixels (tiles * 8 = 256). */
    constexpr u16 viewport_px() { return viewport_tx() << 3; }
    /** @brief Viewport height in pixels (tiles * 8 = 192). */
    constexpr u16 viewport_py() { return viewport_ty() << 3; }
#elif defined(TARGET_GBA)
    // The Game Boy Advance panel is 240x160 -- the first target NARROWER than the
    // NES horizontally (30 tiles vs 32) as well as shorter (20 tiles vs 30). Same
    // family as the DS: the NES game is never compromised, so the GBA shows a
    // 240x160 WINDOW onto the same 256x240 world the game renders, cropping 2 tiles
    // (16px) of width and 10 tiles (80px) of height. The backend (src/gba/video.cpp)
    // maps the NES PPU onto the GBA's native 2D hardware (BG tilemap + hardware OBJ
    // + per-scanline HBlank scroll), so raster splits and sprite-0 still work.
    //
    // viewport_tx()/ty() are the VISIBLE window only. The emulated PPU VRAM stays
    // generous against the viewport (the nametable is 32 tiles wide regardless),
    // which is where the correctness margin for sub-tile scroll + lookahead lives --
    // NOT in these accessors. Any code reading viewport_tx() must not assume 32, and
    // any code reading viewport_ty() must not assume 30.
    /** @brief Viewport width in tiles (GBA: 30 = the 240px panel, < NES 32). */
    constexpr u16 viewport_tx() { return 30; }
    /** @brief Viewport height in tiles (GBA: 20 = the 160px panel, < NES 30). */
    constexpr u16 viewport_ty() { return 20; }
    /** @brief Viewport width in pixels (tiles * 8 = 240). */
    constexpr u16 viewport_px() { return viewport_tx() << 3; }
    /** @brief Viewport height in pixels (tiles * 8 = 160). */
    constexpr u16 viewport_py() { return viewport_ty() << 3; }
#elif defined(TARGET_NX) || defined(TARGET_WIIU)
    // The Switch and Wii U are 16:9 consoles, so unlike the 4:3 GX/3DS consoles
    // they do NOT pillarbox a 256-wide frame. Instead they widen the viewport
    // (the same "render more of the world horizontally" model as the SDL
    // LANDSCAPE desktop path), keeping the NES's 30-tile height. 52 tiles (416px)
    // x 30 (240px) is ~16:9; the height stays a multiple of the source so a clean
    // integer 3x fills 720p vertically, and the backend (src/switch/video.cpp,
    // src/wiiu/video.cpp) scales the width to fill the panel. 52 is a multiple of
    // 4 tiles, keeping the 32px attribute regions aligned (matching the SDL
    // path's `& ~3u`). VRAM is the same two pages (0x800) the SDL path uses for
    // any sub-512px render width.
    /** @brief Viewport width in tiles (Switch/Wii U: 52, widescreen). */
    constexpr u16 viewport_tx() { return 52; }
    /** @brief Viewport height in tiles (Switch/Wii U: 30). */
    constexpr u16 viewport_ty() { return 30; }
    /** @brief Viewport width in pixels (tiles * 8). */
    constexpr u16 viewport_px() { return viewport_tx() << 3; }
    /** @brief Viewport height in pixels (tiles * 8). */
    constexpr u16 viewport_py() { return viewport_ty() << 3; }
#elif defined(LANDSCAPE)
    /** @brief Viewport width in tiles — flexible on landscape, derived from window width (runtime). */
    constexpr u16 viewport_tx() { return ((mode->w / scale) >> 3) & ~3u; }
    /** @brief Viewport height in tiles — pinned to 30, matching the axis `scale` was chosen for. */
    constexpr u16 viewport_ty() { return 30; }
    /** @brief Viewport width in pixels (tiles * 8) — runtime, follows ::video::viewport_tx. */
    constexpr u16 viewport_px() { return viewport_tx() << 3; }
    /** @brief Viewport height in pixels (tiles * 8). */
    constexpr u16 viewport_py() { return viewport_ty() << 3; }
#else
    /** @brief Viewport width in tiles — pinned to 32, matching the axis `scale` was chosen for. */
    constexpr u16 viewport_tx() { return 32; }
    /** @brief Viewport height in tiles — flexible on portrait, derived from window height (runtime). */
    constexpr u16 viewport_ty() { return (mode->h / scale) >> 3; }
    /** @brief Viewport width in pixels (tiles * 8). */
    constexpr u16 viewport_px() { return viewport_tx() << 3; }
    /** @brief Viewport height in pixels (tiles * 8) — runtime, follows ::video::viewport_ty. */
    constexpr u16 viewport_py() { return viewport_ty() << 3; }
#endif
}


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
   * @brief Latches a ::scroll_t into the PPU scroll registers (NES).
   * @param s A ::scroll_t produced by ::ppu::CartesianToScroll.
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

namespace video {
    /**
     * @brief Blocks until the renderer has presented the current frame.
     *
     * On NES builds this waits for VBlank via ::PPUSTATUS; on desktop it
     * waits on the SDL3 present fence.
     */
    void WaitForPresent();
}

namespace ppu {
    /**
     * @brief Enables rendering by writing to ::PPUCTRL and ::PPUMASK.
     * @param ppuCtrl_ Value to latch into ::PPUCTRL (see ::ppu::ctrl flags).
     * @param ppuMask_ Value to latch into ::PPUMASK (see ::ppu::mask flags).
     */
    void EnableRendering(u8 ppuCtrl_, u8 ppuMask_);

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

    /**
     * @brief Sets the absolute scroll of the screen.
     * @param x New horizontal scroll, in pixels.
     * @param y New vertical scroll, in pixels.
     */
    void SetScroll(u16 x, u16 y);

    /**
     * @brief Adds a signed delta to the current scroll.
     * @param x Horizontal delta, in pixels.
     * @param y Vertical delta, in pixels.
     */
    void DeltaScroll(i8 x, i8 y);

    /**
     * @brief Writes an array of bytes into nametable memory with a stride.
     *
     * Copies @p source byte-by-byte starting at the tile position
     * corresponding to (@p x, @p y). @p polarity selects horizontal
     * (stride 1) or vertical (stride 32) writes, matching ::ppu::ctrl::POLARITY.
     *
     * @param x        Tile X position (pixels / 8).
     * @param y        Tile Y position (pixels / 8).
     * @param source   Source buffer to push into PPU video RAM.
     * @param sBuffer  Size of @p source in bytes.
     * @param polarity Non-zero for vertical writes, zero for horizontal.
     */
    void WriteFromBufferToNameTable(u16 x, u16 y, const u8* source, u8 sBuffer, u8 polarity);

    /**
     * @brief Writes a single byte into nametable memory.
     * @param x     Tile X position.
     * @param y     Tile Y position.
     * @param value Byte value to write.
     */
    void WriteSingleToNameTable(u16 x, u16 y, u8 value);

    /**
     * @brief Writes a single byte into nametable memory at a precomputed address.
     *
     * Address overload of ::ppu::WriteSingleToNameTable. The (x,y)->address projection
     * is the costly part of the write (a divide+modulo by the 30-row nametable height);
     * this lets a caller pay it ONCE, off the hot path, via ::ppu::CartesianToAddress,
     * then replay the write -- e.g. inside the tight vblank window -- as three register
     * pokes with no arithmetic. @p address must be what ::ppu::CartesianToAddress returns
     * for the active backend (a \$2000-based PPU address on NES, a 0-based VRAM offset on
     * desktop). It is `int` so it stays 16-bit on llvm-mos yet widens on hosts.
     *
     * @param address Precomputed VRAM address (see ::ppu::CartesianToAddress).
     * @param value   Byte value to write.
     */
    void WriteSingleToNameTable(int address, u8 value);

    /**
     * @brief Writes bytes produced by a provider callback into nametable memory.
     *
     * Equivalent to ::ppu::WriteFromBufferToNameTable but sources each byte
     * from `fn(i)` instead of a preallocated buffer — useful for patterns and
     * procedurally generated rows.
     *
     * The provider's iteration/index type @p Idx is generic, so a `u8`
     * provider (with a single-byte loop counter) binds directly without
     * forcing the callback signature up to `u16`. The body differs per target
     * (PPU register pokes on NES, host video RAM on SDL3), so it is defined
     * out-of-line in each backend with explicit instantiations for the index
     * types actually used (`u8` and `u16`).
     *
     * @tparam Idx     Parameter type of @p fn (the value handed to the callback).
     * @param x        Tile X position.
     * @param y        Tile Y position.
     * @param fn       Provider returning the byte to write for iteration `i`.
     * @param amt      Number of iterations.
     * @param polarity Non-zero for vertical writes, zero for horizontal.
     */
    template <typename Idx>
    void WriteFromProviderToNameTable(u16 x, u16 y, u8 (*fn)(Idx), u8 amt, u8 polarity);

    /**
     * @brief Writes an array of bytes into the attribute table with a stride.
     *
     * Same layout as ::ppu::WriteFromBufferToNameTable but targets attribute
     * memory instead of the nametable.
     *
     * @param x        Tile X position.
     * @param y        Tile Y position.
     * @param source   Source buffer of attribute bytes.
     * @param sBuffer  Size of @p source in bytes.
     * @param polarity Non-zero for vertical, zero for horizontal.
     */
    void WriteFromBufferToAttributeTable(u16 x, u16 y, const u8* source, u8 sBuffer, u8 polarity);

    /**
     * @brief Writes a single byte into attribute memory.
     * @param x     Tile X position.
     * @param y     Tile Y position.
     * @param value Attribute byte (palette + flip flags).
     */
    void WriteSingleToAttributeTable(u16 x, u16 y, u8 value);

    /**
     * @brief Flushes pending nametable and attribute-table writes to the PPU.
     * @param nt Nametable index to flush.
     * @param at Attribute-table index to flush.
     */
    void Flush(u8 nt, u8 at);

    /**
     * @brief Sets the color emphasis bits in ::PPUMASK (bits 5-7).
     * @param priority OR of ::ppu::mask bits (::ppu::mask::RED, GREEN, BLUE).
     */
    void SetColorPriority(u8 priority);

    /** @brief Palette-RAM writes. */
    namespace pal {
        /**
         * @brief Writes an array of bytes into palette memory.
         * @param offset   Palette RAM offset to start writing at.
         * @param source   Source buffer of palette indices.
         * @param sBuffer  Size of @p source in bytes.
         */
        void WriteFromBuffer(u8 offset, const u8* source, u8 sBuffer);

        /**
         * @brief Writes a single byte into palette memory.
         * @param offset Palette RAM offset.
         * @param value  Palette index to store.
         */
        void WriteSingle(u8 offset, u8 value);
    }
}

namespace oam {
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
    void RefreshSprites(const sprite_t* oam);
}

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
namespace ppu {
/** @brief Write-through register+shadow for ::ppu::raw::PPUCTRL (see ::wo_register). */
extern wo_register<ppu::raw::PPUCTRL>   PPUCTRL;
/** @brief Write-through register+shadow for ::ppu::raw::PPUMASK (see ::wo_register). */
extern wo_register<ppu::raw::PPUMASK>   PPUMASK;
}   // namespace ppu

namespace oam {
/** @brief Write-through register+shadow for ::ppu::raw::OAMADDR (see ::wo_register). */
extern wo_register<ppu::raw::OAMADDR>   OAMADDR;
/** @brief Write-through register+shadow for ::ppu::raw::OAMDMA (see ::wo_register). */
extern wo_register<ppu::raw::OAMDMA>    OAMDMA;
}   // namespace oam

#else
namespace ppu {
/** @brief Desktop equivalent of the ::ppu::PPUCTRL register — a plain shadow byte the renderer reads. */
extern u8 PPUCTRL;
/** @brief Desktop equivalent of the ::ppu::PPUMASK register — a plain shadow byte the renderer reads. */
extern u8 PPUMASK;
}   // namespace ppu

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
