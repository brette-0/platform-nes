#pragma once
#include <platform-nes/platform-nes.hpp>

enum class eGameModes : u8 {
    Title,  // title screen
    World,  // world map, level select
    Level,  // gameplay, platforming in level
};

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
