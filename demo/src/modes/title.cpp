#include "title.hpp"
#include "../main.hpp"
#include "../banks.hpp"
#include "platform-nes/mappers/mmc3.hpp"

namespace title {

    TITLE NI void main() {
        pIRQ = irq_handler;
        pNMI = nmi_handler;
        irq::EnableInterrupts();
        // BG only, deliberately: sprites don't need to be on for the A12
        // edges below (the PPU's idle sprite fetch happens every scanline
        // regardless of the sprite mask bit), and title never touches PPU
        // OAM -- no clear, no ::oam::RefreshSprites, unlike
        // EnterLevelSetup's explicit clear before ITS EnableRendering call.
        // Enabling sprites here would just display whatever garbage
        // happens to be sitting in the PPU's OAM at power-on.
        ppu::EnableRendering(ppu::ctrl::BG_ADDR, ppu::mask::BG | ppu::mask::BG_L);
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
