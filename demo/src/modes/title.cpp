#include "title.hpp"
#include "../main.hpp"
#include "../banks.hpp"
#include "platform-nes/mappers/mmc3.hpp"

namespace title {

    TITLE NI void main() {
        pIRQ = irq_handler;
        pNMI = nmi_handler;
        irq::EnableInterrupts();
        ppu::EnableRendering(ppu::ctrl::BG_ADDR, ppu::mask::BG | ppu::mask::SPRITE | ppu::mask::BG_L | ppu::mask::SPRITE_L);
        mmc3::ScheduleScanlineIRQ(0, {0, 0});
        while (gameMode == eGameModes::Title) {}
        ppu::PPUMASK = 0;
        irq::DisableInterrupts();
    }

    void nmi_handler() {

    }

    void irq_handler() {
        mmc3::AcknowledgeScanlineIRQ();
        gameMode   = eGameModes::Level;
    }
}
