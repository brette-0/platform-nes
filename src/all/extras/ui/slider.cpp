#include <platform-nes/extras/ui/slider.hpp>

#include "platform-nes/video.hpp"
#include "platform-nes/extras/math.hpp"

namespace ui::slider {
    TileSlider::TileSlider(
        const vec2<u16> pos,        const u8 width,
        const u8 defaultPosition,
        const u8 unselectedGraphic, const u8 selectedGraphic
    ) : pos(pos), width(width), unselectedGraphic(unselectedGraphic),
        selectedGraphic(selectedGraphic), selectedPosition(defaultPosition) {
        // slider body is presumed to be drawn, but not the slider piece itself
        // does not edit attributes, as we presume an attribute has 16x16 finity
        ppu::WriteSingleToNameTable({pos.x + selectedPosition, pos.y}, selectedGraphic);
    }

    void TileSlider::Move(const i8 amt) {
        ppu::WriteSingleToNameTable({pos.x + selectedPosition, pos.y}, unselectedGraphic);
        const auto aamt = abs(amt);
        if (amt < 0) {
            selectedPosition -= aamt > selectedPosition
                ? selectedPosition
                : aamt;
        } else {
            selectedPosition += amt;
            if (selectedPosition > width) selectedPosition = width;
        }

        ppu::WriteSingleToNameTable({pos.x + selectedPosition, pos.y}, selectedGraphic);
    }

    auto TileSlider::Pass(const u8 inputs) -> void {

    }
}
