#pragma once

#include <platform-nes/types.hpp>
#include <platform-nes/extras/ui/text.hpp>
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
        auto Pass(u8 inputs) -> void;

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