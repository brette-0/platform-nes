#include "title.hpp"
#include "../main.hpp"
#include "../banks.hpp"
#include "../graphics/colours.hpp"
#include "../graphics/strings.hpp"
#include "platform-nes/mappers/mmc3.hpp"

namespace title {

    TITLE NI void main() {
        pIRQ = irq_handler;
        pNMI = nmi_handler;

        mmc3::SwitchCHRBank(mmc3::chr0Control, 4);
        mmc3::SwitchCHRBank(mmc3::chr1Control, 5);
        mmc3::SwitchCHRBank(mmc3::chr2Control, 0);
        mmc3::SwitchCHRBank(mmc3::chr3Control, 1);
        mmc3::SwitchCHRBank(mmc3::chr4Control, 6);
        mmc3::SwitchCHRBank(mmc3::chr5Control, 7);
        ppu::Flush(0, 0);
        ppu::EnableRendering(ppu::ctrl::BG_ADDR, ppu::mask::BG | ppu::mask::BG_L);

        ppu::WriteFromBufferToNameTable(
            (video::viewport_tx() + sizeof(msg_title)) >> 1, 0,
            SIZED_OBJ(msg_title), 0
        );

        u8 port1, port2;

        while (~(port1 | port2) & input::START) {
            input::PollControllers(&port1, &port2);
        }

        irq::EnableInterrupts();
        mmc3::ScheduleScanlineIRQ(0, {0, 0});
        while (gameMode == eGameModes::Title) {}
        ppu::PPUMASK = 0;
        irq::DisableInterrupts();
    }

    void nmi_handler() {
        ppu::pal::WriteFromBuffer(ppu::BG_0, SIZED_OBJ(titleScreenColours));
    }

    void irq_handler() {
        mmc3::AcknowledgeScanlineIRQ();
        gameMode   = eGameModes::Level;
    }
}
