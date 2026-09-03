#include <platform-nes/extras/ui/tileslider.hpp>

#include "platform-nes/video.hpp"
#include "platform-nes/extras/math.hpp"

namespace ui::slider {
    template <bool vertical>
    TileSlider<vertical>::TileSlider(
        const vec2<u16> pos,        const u8 size,
        const u8 defaultPosition,
        const u8 unselectedGraphic, const u8 selectedGraphic,
        u8 (*post)(u8 inputs)
    ) : pos(pos), size(size), unselectedGraphic(unselectedGraphic),
        selectedGraphic(selectedGraphic), selectedPosition(defaultPosition),
        post(post) {
        // slider body is presumed to be drawn, but not the slider piece itself
        // does not edit attributes, as we presume an attribute has 16x16 finity
        if constexpr (vertical) {
            ppu::WriteSingleToNameTable({pos.x, pos.y + selectedPosition}, selectedGraphic);
        } else {
            ppu::WriteSingleToNameTable({pos.x + selectedPosition, pos.y}, selectedGraphic);
        }
    }

    template <bool vertical>
    NI auto TileSlider<vertical>::Pass(const u8 inputs) -> void {
        Move(post(inputs));
    }

    template <bool vertical>
    void TileSlider<vertical>::Move(const i8 amt) {
        ppu::WriteSingleToNameTable({pos.x + selectedPosition, pos.y}, unselectedGraphic);
        const auto aamt = abs(amt);
        if (amt < 0) {
            selectedPosition -= aamt > selectedPosition
                ? selectedPosition
                : aamt;
        } else {
            selectedPosition += amt;
            if (selectedPosition > size) selectedPosition = size;
        }

        if constexpr (vertical) {
            ppu::WriteSingleToNameTable({pos.x, pos.y + selectedPosition}, selectedGraphic);
        } else {
            ppu::WriteSingleToNameTable({pos.x + selectedPosition, pos.y}, selectedGraphic);
        }
    }
}
