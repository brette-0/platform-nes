#include <platform-nes>
#include <platform-nes/apu.hpp>
#include "header.hpp"
#include "main.hpp"

#include "modes/level.hpp"
#include "modes/title.hpp"
#include "modes/world.hpp"
#include "banks.hpp"

// Spelled out, not `auto`: main.hpp declares this extern with an explicit type,
// and GCC rejects a redeclaration that swaps that for a placeholder. Clang
// allows it, so `auto` here builds on NES and breaks every devkitPro target.
atomic auto gameMode = eGameModes::Title;

void (*pNMI)();
void (*pIRQ)();

NMI(FIXED) {
    pNMI();
}

IRQ(FIXED) {
    pIRQ();
}

RESET {
    // Boot-time fact, not a per-mode one: the APU frame-sequencer/DMC IRQs
    // default ENABLED at power-on, and every mode's own irq_handler only
    // acks whatever IT expects (title's acks the MMC3 mapper IRQ, level's
    // acks it too for the HUD split) -- neither touches $4015/$4017. A
    // spurious APU-sourced IRQ landing before this runs once would never
    // get silenced at its actual source, staying asserted and re-firing
    // the instant RTI clears the I flag -- an unbreakable back-to-back IRQ
    // storm the moment any mode calls irq::EnableInterrupts(). Done once,
    // here, before any mode gets a chance to enable interrupts at all,
    // rather than duplicated in every mode that does.
    apu::DisableFrameIRQ();
    apu::DisableDMCIRQ();

    while (!quit) {
        switch (gameMode) {
            case eGameModes::Level:
                // Farcalled, not a bare call after a manual bank pin: window 1
                // holds level_code_tag for the session because main()'s own
                // loop doesn't return in ordinary play, not because something
                // pinned it in advance. See banks.hpp's ::level_code_tag.
                mmc3::CallInBlock<level_code_tag>(level::main);
                continue;

            case eGameModes::World:
                world::main();
                continue;

            case eGameModes::Title:
                // Farcalled: title::main is now ::COLD (banks.hpp's
                // ::title_tag -- same physical bank as ::cold_tag, distinct
                // tag purely so this reads as title's own call), not
                // resident -- see title.cpp's own comment.
                mmc3::CallInBlock<title_tag>(title::main);
        }
    }
}

