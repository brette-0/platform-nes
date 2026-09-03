#pragma once
#include <platform-nes/types.hpp>
#include <intsh>

using namespace br0::intsh;

// non boolean options in UI

namespace ui::slider {
    class TileSlider {
    public:
        TileSlider(
            vec2<u16> pos, u8 width,
            u8 defaultPosition,
            u8 unselectedGraphic, u8 selectedGraphic
        );

        void Move(i8 amt);
        auto Pass(u8 inputs) -> void;

    private:
        vec2<u16> pos;
        u8 width;
        u8 unselectedGraphic;
        u8 selectedGraphic;
        u8 selectedPosition;
    };
}