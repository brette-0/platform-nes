#include <platform-nes>
#include "header.hpp"
#include "main.hpp"

#include "modes/level.hpp"
#include "modes/title.hpp"
#include "modes/world.hpp"
#include "banks.hpp"

// Spelled out, not `auto`: main.hpp declares this extern with an explicit type,
// and GCC rejects a redeclaration that swaps that for a placeholder. Clang
// allows it, so `auto` here builds on NES and breaks every devkitPro target.
eGameModes gameMode = eGameModes::Level;

void (*pNMI)();
void (*pIRQ)();

NMI(FIXED) {
    pNMI();
}

IRQ(FIXED) {
    pIRQ();
}

RESET {
    pNMI = level::nmi_handler;  // rig trampoline
    pIRQ = level::irq_handler;

    while (!quit) {
        switch (gameMode) {
            case eGameModes::Level:
                EnterLevelBanks();
                level::main();
                continue;

            case eGameModes::World:
                world::main();
                continue;

            case eGameModes::Title:
                title::main();
        }
    }
}

