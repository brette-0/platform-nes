/**
 * @file internal.hpp
 * @brief Shared declarations for the Nintendo DS / DSi backend (libnds + calico).
 *
 * Unlike the SDL/OGC/CTR/NX/WIIU backends, the DS does NOT rasterise the PPU on
 * the CPU. The DS is a 2D tile console whose hardware is a near-cousin of the
 * NES PPU, so this backend maps the NES PPU onto the console's NATIVE 2D
 * hardware (Model B): the CHR pattern tables become DS character (tile) VRAM,
 * the nametable + attributes become a DS BG tilemap, the OAM shadow becomes
 * hardware OBJ, and the raster band timeline (::emu::GenerateBands) becomes a
 * per-scanline HBlank scroll table -- so mid-frame scroll changes and the
 * sprite-0 split still work. See src/nds/video.cpp for the full rationale.
 *
 * CMake sets TARGET_NDS (the engine headers gate on it: native 2D hardware,
 * 32x24 viewport, no SDL headers). The DSi build additionally sets TARGET_DSI.
 * The console runs at a real ~60Hz, so frame pacing is the hardware VBlank.
 */
#ifndef NDS_INTERNAL_H
#define NDS_INTERNAL_H

#include <intsh>
using namespace br0::intsh;

/** @brief Application-defined NMI handler (the per-frame VBlank callback). */
extern void nmi_vector();
/** @brief Quit flag; defined in the shared core (src/emu/ppu.cpp). */
extern int quit;

#endif // NDS_INTERNAL_H
