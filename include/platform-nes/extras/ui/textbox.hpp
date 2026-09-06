#pragma once

#include <platform-nes/types.hpp>
#include <platform-nes/extras/ui/text.hpp>
#include <intsh>

using namespace br0::intsh;

namespace ui::button {
    // TextBox disabled for now -- see [[remove Scrollable Text feature]] follow-up.
    /*
    class TextBox {
    public:
        TextBox(
            vec2<u16> pos, vec2<u8> box, bool defaultState,
            u8* offBuff, u8 sOffBuff,
            u8* onBuff, u8 sOnBuff,
            u8 splitter
        );
        // buf is unused: a toggle here redraws the whole box (text::Draw,
        // an arbitrary-length run of bytes), not a single tile op -- doesn't
        // fit the small fixed-capacity buffer, so this still writes
        // directly. Takes buf anyway so Canvas::DispatchItem's uniform
        // Pass(inputs, buf) call still compiles for this item type.
        auto Pass(u8 inputs, u8*& buf) -> void;

        bool enabled;
    private:
        auto Toggle() -> void;
        vec2<u16> pos;
        vec2<u8> box;
        const u8 splitter;
        const u8 sOffBuff;
        const u8 sOnBuff;
        u8* offBuff;
        u8* onBuff;
    };
    */
}