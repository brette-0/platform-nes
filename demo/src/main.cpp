#include <platform-nes>
#include "header.hpp"
#include "main.hpp"

#include "modes/level.hpp"
#include "modes/title.hpp"
#include "modes/world.hpp"
#include "banks.hpp"

auto gameMode = eGameModes::Level;

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
                level::main();
                continue;

            case eGameModes::World:
                mmc3::Call<world::main>();
                continue;

            case eGameModes::Title:
                mmc3::Call<title::main>();
        }
    }
}

