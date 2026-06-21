/**
 * @file internal.hpp
 * @brief Shared declarations for the native Nintendo Wii U backend (devkitPPC +
 *        wut + the SDL2 Wii U portlib).
 *
 * This is the NATIVE Wii U target (a Cafe OS .rpx / .wuhb), NOT the vWii .dol
 * the libogc/GX branch produces. The Espresso (PPC 750, tri-core, big-endian)
 * is wildly overpowered for an NES frame, so -- like the Switch/3DS/SDL
 * backends -- this is an emulated-PPU build: it shares the portable software PPU
 * core (src/emu) and only differs in how a finished frame reaches the screen.
 *
 * The portlib SDL2 (devkitPro's wiiu-sdl2 package) wraps GX2 + ProcUI + the TV/
 * DRC scan buffers + VPAD/KPAD controllers internally, so the whole backend is
 * ordinary SDL2: a streaming ARGB8888 texture scaled to the window (video.cpp),
 * an SDL audio device fed from the bundled WAV (audio.cpp) and SDL_GameController
 * input (input.cpp). This is SDL2 -- entirely separate from the SDL3 desktop
 * backend -- so no code is shared with src/SDL3.
 *
 * CMake sets TARGET_WIIU; the engine headers gate on it alongside TARGET_NX for
 * the widescreen (52x30 == 416x240, ~16:9) viewport and to keep the SDL3 headers
 * out (this backend pulls in SDL2 itself).
 */
#ifndef WIIU_INTERNAL_H
#define WIIU_INTERNAL_H

#include <intsh>
using namespace br0::intsh;

/** @brief Application-defined NMI handler (the per-frame VBlank callback). */
extern void nmi();
/** @brief Quit flag; defined in the shared core (src/emu/ppu.cpp). */
extern int quit;

/** @brief Opens the SDL game controllers (called once from video init()). */
void input_init();

#endif // WIIU_INTERNAL_H
