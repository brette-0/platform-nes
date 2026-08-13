#include <platform-nes>
#include "header.hpp"
#include "main.hpp"

#include "modes/level.hpp"
#include "modes/title.hpp"

auto gameMode = eGameModes::Level;

void (*pNMI)();
void (*pIRQ)();


NMI() {
    pNMI();
}

IRQ() {
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
                continue;

            case eGameModes::Title:
                title::main();
        }
    }
}

