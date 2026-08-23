#pragma once
#include <platform-nes/platform-nes.hpp>

enum class eGameModes : u8 {
    Title,  // title screen
    World,  // world map, level select
    Level,  // gameplay, platforming in level
};

/** @brief Viewport width in metatiles (tiles / 2). */
constexpr u8 viewport_mx() { return video::viewport_tx() >> 1; }
/** @brief Viewport height in metatiles (tiles / 2). */
constexpr u8 viewport_my() { return video::viewport_ty() >> 1; }

extern atomic eGameModes gameMode;

// Mode-dispatched NMI/IRQ entry points: main.cpp's nmiTrampoline/irqTrampoline
// (the only functions actually pinned to the hardware vectors) call through
// these once per interrupt. The active mode's setup path points them at its
// own handlers before enabling interrupts. Plain function pointers, called
// with ordinary C++ call syntax from the trampolines -- never reached by raw
// asm symbol text -- so, unlike the trampolines themselves, neither these nor
// the handlers they point at need C linkage or any special attribute. Left
// uninitialized (zero) so both land in BSS rather than .data.
extern void (*pNMI)();
extern void (*pIRQ)();

// Shared OAM staging buffer -- every mode (title, level, ...) refreshes its
// own sprites into the same 64-sprite table rather than each owning one, so
// it lives here instead of any one mode's header.
extern oam::sprite_t OAMBuffer[64];
