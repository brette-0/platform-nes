#include <platform-nes/extras/ui/button.hpp>

#include "platform-nes/video.hpp"

namespace ui::button {
    TickBox::TickBox(const vec2<u16> pos, const bool defaultState)
        : enabled(defaultState), pos(pos) {
        ppu::WriteSingleToNameTable(pos, enabled);
    }

    AI auto TickBox::Toggle() -> bool {
        ppu::WriteSingleToNameTable(pos, enabled ^= true);
        return enabled;
    }
}
