#pragma once
#include <platform-nes/types.hpp>
#include <platform-nes/extras/ui/writeop.hpp>
#include <intsh>

using namespace br0::intsh;

// non boolean options in UI

namespace ui::slider {
    template <bool vertical>
    class TileSlider {
    public:
        TileSlider(
            vec2<u16> pos, u8 size,
            u8 defaultPosition,
            u8 unselectedGraphic, u8 selectedGraphic,
            u8 (*post)(u8 inputs)
        );

        // Queues the erase-old/draw-new writes into q instead of touching
        // the PPU directly -- see WriteQueue's own comment.
        auto Pass(u8 inputs, WriteQueue& q) -> void;

    private:
        void Move(i8 amt, WriteQueue& q);

        vec2<u16> pos;
        u8 size;
        u8 unselectedGraphic;
        u8 selectedGraphic;
        u8 selectedPosition;
        u8 (*post)(u8 inputs);
    };
}