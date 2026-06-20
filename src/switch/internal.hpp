/**
 * @file internal.hpp
 * @brief Shared declarations for the Nintendo Switch backend (devkitA64 + libnx).
 *
 * Like the GameCube/Wii (GX) and 3DS (citro2d) backends, the Switch is an
 * emulated-PPU build: it shares the portable software PPU core (src/emu) and
 * differs only in how a finished frame reaches the screen. The Tegra is wildly
 * overpowered for a 256x240 NES frame, so rather than a GPU tilemap this backend
 * keeps it simple -- ::emu::GenerateFrame rasterises into a 256x240 ARGB8888
 * staging buffer and the CPU integer-scales it into the libnx framebuffer
 * (nwindow). See src/switch/video.cpp for the scaling/letterbox details.
 *
 * CMake sets TARGET_NX (the engine headers gate on it alongside TARGET_NES /
 * TARGET_OGC / TARGET_CTR: emulated PPU, fixed 32x30 viewport, no SDL headers).
 * The console runs at a real 60Hz and the framebuffer present is vsync-locked,
 * so that is the frame pacing.
 */
#ifndef NX_INTERNAL_H
#define NX_INTERNAL_H

#include <intsh>
using namespace br0::intsh;

/** @brief Application-defined NMI handler (the per-frame VBlank callback). */
extern void nmi();
/** @brief Quit flag; defined in the shared core (src/emu/ppu.cpp). */
extern int quit;

/** @brief Initialises the libnx gamepad (called once from video init()). */
void input_init();

#endif // NX_INTERNAL_H
