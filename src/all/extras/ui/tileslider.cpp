#include <platform-nes/extras/ui/tileslider.hpp>

#include "platform-nes/video.hpp"
#include "platform-nes/extras/math.hpp"
#include "platform-nes/technology.hpp"   // CREATE_SEGMENT_KEYWORD / MODULE_PLACEMENT

// WHERE THIS MODULE'S CODE GOES -- see PLATFORM_NES_UI_SECTION's own comment
// in CMakeLists.txt/local.cmake.example. Compulsory, like audio's: no default,
// so the placement decision is never a silent guess. Guarded the same way
// MODULE_PLACEMENT itself is (technology.hpp): this file lives under
// src/all/, compiled for every backend, not just NES-banked ones, so the
// requirement only applies where placement is even possible.
#if defined(TARGET_NES) && defined(NES_MAPPER_BANKSWITCHED)
#ifndef PLATFORM_NES_UI_SECTION
#error "PLATFORM_NES_UI_SECTION is not set. It names the ELF section \
src/all/extras/ui/*.cpp is placed into. Set it from CMake -- \
local.cmake.example has a worked example."
#endif
#endif
#define UI_BANK MODULE_PLACEMENT(PLATFORM_NES_UI_SECTION)

namespace ui::slider {
    template <bool vertical>
    UI_BANK TileSlider<vertical>::TileSlider(
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
    UI_BANK auto TileSlider<vertical>::Pass(const u8 inputs) -> void {
        Move(post(inputs));
    }

    template <bool vertical>
    UI_BANK void TileSlider<vertical>::Move(const i8 amt) {
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
