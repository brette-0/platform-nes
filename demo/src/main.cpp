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
                // NOT mmc3::Call<level::main>(): level's domain is 16 KiB
                // across BOTH switchable windows, and a farcall switches one
                // register (mmc3::CallInSection). Map the pair here, once,
                // then call directly -- safe because this dispatcher is
                // ::FIXED and so is not in either window it is replacing.
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

