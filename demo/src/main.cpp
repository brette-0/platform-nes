#include <platform-nes>
#include <platform-nes/apu.hpp>
#include <platform-nes/mappers/mmc3.hpp>
#include "header.hpp"
#include "main.hpp"

#include "modes/level.hpp"
#include "modes/title.hpp"
#include "modes/world.hpp"
#include "banks.hpp"


// ReSharper disable once CppUseAuto
atomic eGameModes gameMode = eGameModes::Title;

SYSMEM oam::sprite_t OAMBuffer[64] __attribute__((aligned(256)));

void (*pNMI)();
void (*pIRQ)();

NMI(FIXED) {
    pNMI();
}

IRQ(FIXED) {
    pIRQ();
}

RESET {
    apu::DisableFrameIRQ();
    apu::DisableDMCIRQ();

    while (!quit) {
        switch (gameMode) {
            case eGameModes::Level:
                mmc3::CallInBlock<level_code_tag>(level::main);
                continue;

            case eGameModes::World:
                world::main();
                continue;

            case eGameModes::Title:
                mmc3::CallInBlock<title_tag>(title::main);
        }
    }
}

