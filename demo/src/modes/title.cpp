#include "title.hpp"
#include "../main.hpp"
#include "../banks.hpp"
#include "../graphics/colours.hpp"
#include "platform-nes/mappers/mmc3.hpp"

namespace title {

    TITLE NI void main() {
        pIRQ = irq_handler;
        pNMI = nmi_handler;
        irq::EnableInterrupts();

        // Blank nametable/attribute data -- invisible regardless of timing
        // since rendering's still off, so the PPU isn't fetching it yet.
        // The BG palette write moved to nmi_handler (below): palette RAM is
        // live the instant it's written, even with rendering off (see
        // level.cpp's own EnterLevelSetup comment on this), so NMI -- which
        // only ever runs during vblank -- is the one place a palette write
        // is guaranteed safe by construction, not just safe-by-accident of
        // nothing else being on screen yet.
        ppu::Flush(0, 0);

        ppu::EnableRendering(ppu::ctrl::BG_ADDR, ppu::mask::BG | ppu::mask::BG_L);
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
