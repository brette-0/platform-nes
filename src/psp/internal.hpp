/**
 * @file internal.hpp
 * @brief Shared declarations for the Sony PSP backend (pspsdk, raw VRAM).
 *
 * Like the Switch/Wii U backends, the PSP presents the shared software PPU
 * core (src/emu) rather than driving any native tile hardware: the Allegrex
 * is easily fast enough for a 256x240 NES frame, so ::emu::GenerateFrame
 * rasterises into an ARGB8888 staging buffer and the CPU nearest-neighbour
 * scale-blits it straight into VRAM. No GU (the PSP's GE/3D pipeline) is
 * used at all -- see src/psp/video.cpp for the rationale, matching the
 * Switch backend's own choice to skip deko3d for a first pass.
 *
 * CMake sets TARGET_PSP (the engine headers gate on it alongside TARGET_NX /
 * TARGET_WIIU: emulated PPU, widescreen ~16:9 viewport, no SDL headers). The
 * console runs at a real 60Hz; frame pacing comes from sceDisplayWaitVblankStart.
 */
#ifndef PSP_INTERNAL_H
#define PSP_INTERNAL_H

#include <intsh>
using namespace br0::intsh;

/** @brief Application-defined NMI handler (the per-frame VBlank callback). */
extern void nmi_vector();
/** @brief Quit flag; defined in the shared core (src/emu/ppu.cpp). */
extern int quit;

/** @brief Registers the HOME-menu exit callback (called once from video init()). */
void psp_setup_exit_callback();

/** @brief Initialises the PSP controller sampling (called once from video init()). */
void input_init();

#endif // PSP_INTERNAL_H
