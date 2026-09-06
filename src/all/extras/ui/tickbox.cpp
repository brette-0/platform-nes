#include <platform-nes/extras/ui/tickbox.hpp>

#include "platform-nes/video.hpp"
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
#define UI_BANK MODULE_PLACEMENT(PLATFORM_NES_UI_SECTION) MINSIZE

namespace ui::button {
    UI_BANK int TickBox::Make(const vec2<u16> pos) {
        return ppu::CartesianToAddress(pos);
    }

    UI_BANK TickBox::TickBox(const vec2<u16> pos, const bool defaultState)
        : enabled(defaultState), addr(Make(pos)) {
    }

    UI_BANK auto TickBox::Pass(const u8, u8*& buf) -> void {
        Toggle(buf);
    }
}
