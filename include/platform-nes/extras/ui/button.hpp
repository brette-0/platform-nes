#pragma once
#include <platform-nes/types.hpp>
#include <intsh>

using namespace br0::intsh;

namespace ui::button {
    class TickBox {
    public:
        TickBox(vec2<u16> pos, bool defaultState);
        auto Toggle() -> bool;
        auto Pass(u8 inputs) -> void;

        bool enabled;
    private:
        vec2<u16> pos;
    };
}
