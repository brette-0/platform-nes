#include <platform-nes/extras/ui/textbox.hpp>

#include "platform-nes/technology.hpp"

namespace ui::button {
    TextBox::TextBox(
        const vec2<u16> pos, const vec2<u8> box, const bool defaultState,
        u8* offBuff, const u8 sOffBuff,
        u8* onBuff, const u8 sOnBuff,
        const text::Alignment align, u8 const splitter
    ) : enabled(defaultState), pos(pos), box(box), splitter(splitter),
        align(align), sOffBuff(sOffBuff), sOnBuff(sOnBuff),
        offBuff(offBuff), onBuff(onBuff) {
        text::Draw(
            enabled ? onBuff : offBuff,
            enabled ? sOnBuff : sOffBuff,
            pos, box, splitter, align
        );
    }

    AI auto TextBox::Toggle() -> void {
        enabled ^= true;
        text::Draw(
            enabled ? onBuff : offBuff,
            enabled ? sOnBuff : sOffBuff,
            pos, box, splitter, align
        );
    }

    NI auto TextBox::Pass(u8 inputs) -> void {
        Toggle();
    }
}
