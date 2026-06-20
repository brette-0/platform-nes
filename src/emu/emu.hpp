/**
 * @file emu.hpp
 * @brief Internal seam between the portable software-PPU core and a presenting
 *        backend (SDL3 desktop, libogc/GX on GameCube + Wii).
 *
 * Everything that emulates the NES PPU -- the per-pixel renderer, the
 * nametable/attribute/palette/OAM write functions, the scroll model and the
 * sprite-zero machinery -- is platform-agnostic and lives in src/emu/. A
 * backend supplies only the things that genuinely differ between platforms:
 * opening a display, pacing frames, presenting a finished framebuffer, polling
 * input and driving audio.
 *
 * The single rendering seam is ::emu::GenerateFrame: the core fills a caller-
 * owned ARGB8888 surface, and the backend is responsible for getting those
 * pixels on screen (SDL streams them into a texture; OGC swizzles them into a
 * GX texture and draws a quad). This keeps the ~170-line PPU emulator in one
 * place instead of duplicated per backend.
 */
#ifndef EMU_H
#define EMU_H

#include <intsh>
using namespace br0::intsh;

namespace oam { struct sprite_t; }

namespace emu {

/**
 * @brief Renders one full PPU frame into a caller-provided ARGB8888 surface.
 *
 * Reads the shared PPU state (::VideoRAM, ::paletteRAM, ::patternTable,
 * ::ppu::PPUCTRL/PPUMASK, ::xScroll/::yScroll, the OAM snapshot and the queued
 * scanline IRQs) and writes one 32-bit pixel per cell. The backend owns the
 * buffer and its presentation; the core never touches the display.
 *
 * @param fb     Destination ARGB8888 pixels (0xAARRGGBB), at least
 *               ::video::viewport_py() rows of @p stride pixels.
 * @param stride Row stride in pixels (u32 units), >= ::video::viewport_px().
 */
void GenerateFrame(u32* fb, int stride);

/**
 * @brief Allocates the emulated video and palette RAM.
 *
 * Sets the shared ::VideoRAM (@p vram_bytes) and ::paletteRAM (32 bytes)
 * pointers the write functions operate on. Called once from the backend's
 * lifecycle init, where the VRAM size policy lives (fixed two pages on OGC,
 * window-derived on SDL).
 *
 * @param vram_bytes Size of the emulated nametable/attribute VRAM, in bytes.
 */
void InitMemory(unsigned vram_bytes);

/**
 * @brief Per-band emit callback for ::emu::GenerateBands.
 *
 * Invoked once per contiguous run of scanlines [@p y0, @p y1) that share a
 * constant scroll. @p xscroll / @p yscroll are the absolute PPU scroll in
 * effect at @p y0; within the band, screen row @c sy sources world row
 * `yscroll + (sy - y0)` (the PPU's per-scanline Y auto-increment).
 */
using band_emit_fn = void (*)(int y0, int y1, u16 xscroll, u16 yscroll);

/**
 * @brief Drives the frame's raster IRQ timeline without per-pixel work.
 *
 * The GameCube/Wii (GX) backend does not rasterise on the CPU; instead it lets
 * Flipper draw the nametable as a textured tilemap. But the game's per-frame
 * logic runs inside the scanline IRQ handlers (e.g. the sprite-zero split at
 * py=16 that re-points the playfield scroll), so the timeline must still be
 * walked and the handlers fired in raster order. This does exactly that: it
 * fires each queued IRQ at its scanline (running game logic, which may move the
 * scroll), and calls @p emit for every band of scanlines between IRQs with the
 * scroll that band should render with. It performs no rendering itself.
 *
 * Shares the scroll / sprite-zero / yScroll-written machinery with
 * ::emu::GenerateFrame, so the two stay byte-for-byte consistent on the split.
 *
 * @param emit Backend band renderer (see ::emu::band_emit_fn).
 */
void GenerateBands(band_emit_fn emit);

/**
 * @brief Pointer to the PPU-side OAM snapshot the renderer draws sprites from.
 *
 * The shared core owns the OAM shadow (frozen each frame by
 * ::oam::RefreshSprites, the OAMDMA analogue). The GX backend reads it directly
 * to emit sprite quads. Always ::OAM_SPRITES entries.
 */
const oam::sprite_t* OamShadow();

}   // namespace emu

#endif // EMU_H
