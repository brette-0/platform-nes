/**
 * @file internal.hpp
 * @brief Shared declarations for the Nintendo 3DS backend (libctru + citro2d).
 *
 * Like the GameCube/Wii (GX) backend, the 3DS presents the shared PPU core
 * (src/emu) GPU-natively -- it does NOT rasterise on the CPU. ::emu::GenerateBands
 * walks the IRQ timeline and the PICA200 draws the nametable + sprites as
 * textured quads via citro2d/citro3d. The PICA200 has no paletted-texture
 * format, so rather than a CI4 atlas + TLUTs (the GX approach) the palette is
 * baked into RGBA8 CHR atlases (one per palette), rebuilt only on a paletteRAM
 * change. See src/3ds/video.cpp for the full rationale.
 *
 * CMake sets TARGET_CTR (the engine headers gate on it alongside TARGET_NES /
 * TARGET_OGC: emulated PPU, fixed 32x30 viewport, no SDL headers). The console
 * runs at a real 60Hz, so frame pacing is the GPU's own VBlank present.
 */
#ifndef CTR_INTERNAL_H
#define CTR_INTERNAL_H

#include <intsh>
using namespace br0::intsh;

/** @brief Application-defined NMI handler (the per-frame VBlank callback). */
extern void nmi_vector();
/** @brief Quit flag; defined in the shared core (src/emu/ppu.cpp). */
extern int quit;

#endif // CTR_INTERNAL_H
