#pragma once

#include <intsh>

#include "text.hpp"
#include "platform-nes/types.hpp"

using namespace br0::intsh;

namespace ui::choice {
    template <u8 nOptions>
    class SingleChoice {
    public:
        SingleChoice(
            u8* buff, u8 sBuff, vec2<u16> pos, vec2<u8> box,
            u8 wordSplitter, u8 optionSplitter, text::Alignment align,
            u8 emptyGraphic, u8 arrowGraphic
        );

        auto Pass(u8 inputs) -> void;
    private:
        vec2<u16> pos;
        vec2<u8>  box;
        u8 option;
        u8 optionPos[nOptions];
        const u8 emptyGraphic;
        const u8 arrowGraphic;

        auto Next() -> void;
        auto Previous() -> void;
    };
}