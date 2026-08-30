#include "title.hpp"
#include "../main.hpp"
#include "../banks.hpp"
#include "../graphics/colours.hpp"
#include "../graphics/strings.hpp"
#include "platform-nes/mappers/mmc3.hpp"

namespace title {
    // ReSharper disable once CppUseAuto
    atomic u8 menuOption = NewGame;
    // ReSharper disable once CppUseAuto
    static atomic u8 lastMenuOption = NewGame;
    static oam::oam_t Clear(u16 _);


    TITLE NI void main() {
        oam::PopulateFromProvider(OAMBuffer, 0, oam::y, Clear, 64);
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
        ppu::EnableRendering(ppu::ctrl::SPRITE_ADDR | ppu::ctrl::GEN_NMI, ppu::mask::BG_L | ppu::mask::SPRITE_L);

        u8 port1, port2, prevInputs = 0;

        while (true) {
            input::PollControllers(&port1, &port2);

            const auto inputs  = port1 | port2;
            const auto pressed = inputs & static_cast<u8>(~prevInputs); // strobe: only the frame a button goes down
            prevInputs = inputs;

            if (pressed & (input::UP | input::DOWN)) {
                menuOption -= (pressed & input::UP)   == input::UP;
                if (menuOption > End) menuOption = 0;
                menuOption += (pressed & input::DOWN) == input::DOWN;
                if (menuOption > End) menuOption = End;
            }

            // Every iteration reaches WaitForPresent exactly once, regardless
            // of which branch above ran -- on SDL3 that's what pumps the OS
            // event queue, paces to 60Hz, and presents the frame. Skipping it
            // on the idle (no input) path -- as this loop used to -- leaves
            // desktop targets spinning with no event pump, which reads to the
            // OS as a hung window. NES/console backends don't need the pump
            // but still want the one-call-per-frame NMI/present pacing.
            bool proceed = false;
            if (pressed & input::A) {
                switch (menuOption) {
                    case NewGame:
                        proceed = true;
                        break;

                    case Continue:
                    case Options:
                        break;

#if defined(TARGET_MACOS) || defined(TARGET_WINDOWS) || defined(TARGET_LINUX)
                    case Quit:
                        quit = true;
                        return;
#endif

                    default: ;
                }
            }

            video::WaitForPresent();
            if (quit) return;
            if (proceed) break;
        }

        ppu::PPUMASK = 0;
        gameMode = eGameModes::Level;
    }

    void nmi_handler() {
        const auto bandTop = (((viewport_my() + 1) >> 1) - 2) << 2;
        const u16 menuCol  = (viewport_mx() << 1) - 1 - sizeof(msg_continue);

        // write chrEmpty_tile where arrow was
        ppu::WriteSingleToNameTable(menuCol - 2, bandTop + 1 + lastMenuOption, chrEmpty_tile);
        // write chrArrow_tile where arrow now is
        ppu::WriteSingleToNameTable(menuCol - 2, bandTop + 1 + menuOption, chrArrow_tile);
        lastMenuOption = menuOption;

        // re-DMA OAM every frame -- OAM decays if it isn't refreshed
        // regularly, and now that sprites are enabled that matters here too.
        oam::RefreshSprites(OAMBuffer);

        ppu::SetScroll(0, 0);
    }

    void irq_handler() {
        mmc3::AcknowledgeScanlineIRQ();
        gameMode   = eGameModes::Level;
    }

    void InitTitleScreen() {
        // write attributes horizontally (palette 0) for bottom most six rows of screen
        const auto bandTop = (((viewport_my() + 1) >> 1) - 2) << 2;
        // Ceiling-divide, not viewport_mx()>>1 (== viewport_tx()>>2, floor):
        // every other backend's viewport width is a multiple of 4 tiles, but
        // the 3DS's fixed 50-tile viewport isn't (50/4 = 12.5) -- floor
        // division under-covers by one cell, leaving the trailing partial
        // attribute column (the last 2 tile-columns) at Flush's default
        // palette 3 instead of this band's palette 0.
        const u8 attrCells = static_cast<u8>((video::viewport_tx() + 3) >> 2);
        for (auto r = bandTop; r < viewport_my() << 1; r += 4) {
            ppu::WriteFromProviderToAttributeTable(
                0, r,
                MenuAttributesProvider, attrCells, 0
            );
        }

         ui::text::DrawText(
             SIZED_OBJ(msg_title),
             {1, bandTop + 1},
     {static_cast<u8>((viewport_mx() >> 1) - 1), 3}, chrEmpty_tile,
             ui::text::Alignment::Left
         );

        // menu items -- all start at the same column, with a 1-tile gap from
        // the right edge for the longest entry (msg_newGame/msg_continue).
        const u16 menuCol = (viewport_mx() << 1) - 1 - sizeof(msg_continue);

        // selection cursor -- starts on New Game, one tile of gap before the text.
        ppu::WriteSingleToNameTable(menuCol - 2, bandTop + 1, chrArrow_tile);

        ppu::WriteFromBufferToNameTable(menuCol, bandTop + 1, SIZED_OBJ(msg_newGame), 0);
        ppu::WriteFromBufferToNameTable(menuCol, bandTop + 2, SIZED_OBJ(msg_continue), 0);
        ppu::WriteFromBufferToNameTable(menuCol, bandTop + 3, SIZED_OBJ(msg_options), 0);
#if defined(TARGET_MACOS) || defined(TARGET_WINDOWS) || defined(TARGET_LINUX)
        // consoles have no OS to quit back to -- PC targets only.
        ppu::WriteFromBufferToNameTable(menuCol, bandTop + 4, SIZED_OBJ(msg_quit), 0);
#endif
    }

    u8 MenuAttributesProvider(const u8 i) {
        return 0x00;
    }

    static oam::oam_t Clear(const u16) {
        return 0xf0;
    }
}
