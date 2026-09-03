#include <platform-nes/extras/ui/textbox.hpp>

#include "platform-nes/technology.hpp"

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
    UI_BANK TextBox::TextBox(
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

    // ::AI, not ::UI_BANK: must stay reachable without a bank switch from
    // wherever Pass() calls it -- the two are mutually exclusive (see
    // MODULE_PLACEMENT's own doc comment).
    inline AI auto TextBox::Toggle() -> void {
        enabled ^= true;
        text::Draw(
            enabled ? onBuff : offBuff,
            enabled ? sOnBuff : sOffBuff,
            pos, box, splitter, align
        );
    }

    UI_BANK auto TextBox::Pass(u8, u8*&) -> void {
        Toggle();
    }
}
