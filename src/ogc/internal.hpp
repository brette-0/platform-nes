/**
 * @file internal.hpp
 * @brief Shared declarations for the libogc (GameCube + Wii) backend.
 *
 * One backend serves both consoles: they share the same PowerPC "Gekko/Broadway"
 * CPU and the Flipper/Hollywood GX graphics pipeline, so the only divergence is
 * input (GameCube pads via PAD vs. Wii Remotes via WPAD) and a couple of libogc
 * machine flags. libogc exposes those as HW_DOL (GameCube) / HW_RVL (Wii); this
 * project additionally sets TARGET_GC / TARGET_WII (and the TARGET_OGC group)
 * from CMake so the engine headers gate cleanly. A Wii .dol built here also runs
 * unmodified on Wii U via vWii.
 */
#ifndef OGC_INTERNAL_H
#define OGC_INTERNAL_H

#include <gccore.h>
#include <intsh>
using namespace br0::intsh;

/** @brief Application-defined NMI handler (the per-frame VBlank callback). */
extern void nmi();
/** @brief Quit flag; defined in the shared core (src/emu/ppu.cpp). */
extern int quit;

/** @brief Opens the attached controllers (PAD, plus WPAD on Wii). */
void ogc_input_init();

#endif // OGC_INTERNAL_H
