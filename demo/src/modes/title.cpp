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
        ppu::Flush(chrEmpty_tile, 0xff);
        ppu::pal::WriteFromBuffer(0, titleScreenColours, 4);
        InitTitleScreen();
        ppu::SetScroll(0, 0xff);
        ppu::EnableRendering(ppu::ctrl::SPRITE_ADDR | ppu::ctrl::GEN_NMI, ppu::mask::BG | ppu::mask::BG_L);

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
        ppu::SetScroll(0, 0);
    }

    void irq_handler() {
        mmc3::AcknowledgeScanlineIRQ();
        gameMode   = eGameModes::Level;
    }

    void InitTitleScreen() {
        // write attributes horizontally (palette 0) for bottom most six rows of screen
        constexpr auto bandTop = (((viewport_my() + 1) >> 1) - 2) << 2;
        for (auto r = bandTop; r < viewport_my() << 1; r += 4) {
            ppu::WriteFromProviderToAttributeTable(
                0, r,
                MenuAttributesProvider, viewport_mx() >> 1, 0
            );
        }

         ui::text::DrawText(
             SIZED_OBJ(msg_title),
             {1, bandTop + 1},
     {(viewport_mx() >> 1) - 1, 3}, chrEmpty_tile,
             ui::text::Alignment::Left
         );

        // menu items -- all start at the same column, with a 1-tile gap from
        // the right edge for the longest entry (msg_newGame/msg_continue).
        constexpr u16 menuCol = (viewport_mx() << 1) - 1 - sizeof(msg_continue);

        ppu::WriteFromBufferToNameTable(menuCol, bandTop + 1, SIZED_OBJ(msg_newGame), 0);
        ppu::WriteFromBufferToNameTable(menuCol, bandTop + 2, SIZED_OBJ(msg_continue), 0);
        ppu::WriteFromBufferToNameTable(menuCol, bandTop + 3, SIZED_OBJ(msg_options), 0);
        ppu::WriteFromBufferToNameTable(menuCol, bandTop + 4, SIZED_OBJ(msg_quit), 0);
    }

    u8 MenuAttributesProvider(const u8 i) {
        return 0x00;
    }
}
