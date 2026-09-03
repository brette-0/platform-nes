#include <platform-nes/extras/ui/singlechoice.hpp>

#include "platform-nes/input.hpp"

namespace ui::choice {
    template <u8 nOptions>
    SingleChoice<nOptions>::SingleChoice(
            u8* buff, u8 sBuff, const vec2<u16> pos, const vec2<u8> box,
            u8 wordSplitter, u8 optionSplitter, text::Alignment align,
            const u8 emptyGraphic, const u8 arrowGraphic
        ) : pos(pos), box(box), emptyGraphic(emptyGraphic), arrowGraphic(arrowGraphic) {

        // needs space for arrow and space between arrow and text

        for (auto option = 0; option < nOptions; option++) {
            // keep drawing safely across line until optionSplit -> counting lines
            // write the line of the next object (spaces inbetween lines) to optionPos[option]
        }
    }

    template <u8 nOptions>
    auto SingleChoice<nOptions>::Pass(const u8 inputs) -> void {
        if      (inputs & input::UP)   Next();
        else if (inputs & input::DOWN) Previous();
    }

    template<u8 nOptions>
    auto SingleChoice<nOptions>::Next() -> void {
        if (option == nOptions) return;

        option++;
        // draw call to move arrow
    }

    template <u8 nOptions>
    auto SingleChoice<nOptions>::Previous() -> void {
        if (option == 0) return;

        option--;
        // draw call to move arrow
    }
}
