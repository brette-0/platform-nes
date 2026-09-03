#pragma once

#include <platform-nes/types.hpp>
#include <platform-nes/extras/ui/text.hpp>
#include <platform-nes/extras/ui/writeop.hpp>
#include <intsh>

using namespace br0::intsh;

namespace ui::button {
    class TextBox {
    public:
        TextBox(
            vec2<u16> pos, vec2<u8> box, bool defaultState,
            u8* offBuff, u8 sOffBuff,
            u8* onBuff, u8 sOnBuff,
            text::Alignment align, u8 splitter
        );
        // q is unused: a toggle here redraws the whole box (text::Draw, an
        // arbitrary-length run of bytes), not a single tile -- doesn't fit
        // the small fixed-capacity queue, so this still writes directly.
        // Takes q anyway so Canvas::DispatchItem's uniform Pass(inputs, q)
        // call still compiles for this item type.
        auto Pass(u8 inputs, WriteQueue& q) -> void;

        bool enabled;
    private:
        auto Toggle() -> void;
        vec2<u16> pos;
        vec2<u8> box;
        const u8 splitter;
        const text::Alignment align;
        const u8 sOffBuff;
        const u8 sOnBuff;
        u8* offBuff;
        u8* onBuff;
    };
}