/**
 * @file internal.hpp
 * @brief Shared declarations for the Game Boy Advance backend (devkitARM + libgba).
 *
 * Like the Nintendo DS backend (src/nds/), and unlike the SDL/OGC/CTR/NX/WIIU
 * backends, the GBA does NOT rasterise the PPU on the CPU. The GBA is a 2D tile
 * console whose hardware is a near-cousin of the NES PPU, so this backend maps
 * the NES PPU onto the console's NATIVE 2D hardware (Model B): the CHR pattern
 * tables become GBA character (tile) VRAM, the nametable + attributes become a
 * GBA BG tilemap, the OAM shadow becomes hardware OBJ, and the raster band
 * timeline (::emu::GenerateBands) becomes a per-scanline HBlank scroll table --
 * so mid-frame scroll changes and the sprite-0 split still work. See
 * src/gba/video.cpp for the full rationale.
 *
 * CMake sets TARGET_GBA (the engine headers gate on it: native 2D hardware,
 * 30x20 viewport -- the first target NARROWER than the NES, no SDL headers).
 * The console runs at a real ~60Hz, so frame pacing is the hardware VBlank.
 */
#ifndef GBA_INTERNAL_H
#define GBA_INTERNAL_H

#include <intsh>
using namespace br0::intsh;

/** @brief Application-defined NMI handler (the per-frame VBlank callback). */
extern void nmi_vector();
/** @brief Quit flag; defined in the shared core (src/emu/ppu.cpp). */
extern int quit;

#endif // GBA_INTERNAL_H
