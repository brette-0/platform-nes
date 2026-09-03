#include <platform-nes/extras/ui/tickbox.hpp>

#include "platform-nes/video.hpp"

namespace ui::button {
    TickBox::TickBox(const vec2<u16> pos, const bool defaultState)
        : enabled(defaultState), pos(pos) {
        ppu::WriteSingleToNameTable(pos, enabled);
    }

    AI auto TickBox::Toggle() -> void {
        ppu::WriteSingleToNameTable(pos, enabled ^= true);
    }

    NI auto TickBox::Pass(const u8 inputs) -> void {
        Toggle();
    }
}
