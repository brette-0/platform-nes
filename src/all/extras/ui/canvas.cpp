#include <platform-nes/extras/ui/canvas.hpp>

#include "platform-nes/input.hpp"
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

namespace ui {
    template <bool vertical, typename Tuple>
    UI_BANK Canvas<vertical, Tuple>::Canvas(void* buff, const u8 confirmButton) : buff(buff), confirmButton(confirmButton) { }

    // need a draw call for movement of arrow
    template <bool vertical, typename Tuple>
    UI_BANK auto Canvas<vertical, Tuple>::Pass(const u8 inputs, u8*& buf) -> void {
        if constexpr (vertical) {
            if (inputs & input::UP) {
                if (selectedItem > 0) --selectedItem;
                return;
            }
            if (inputs & input::DOWN) {
                if (selectedItem < ItemCount - 1) ++selectedItem;
                return;
            }
        } else {
            if (inputs & input::LEFT) {
                if (selectedItem > 0) --selectedItem;
                return;
            }
            if (inputs & input::RIGHT) {
                if (selectedItem < ItemCount - 1) ++selectedItem;
                return;
            }
        }

        DispatchItem(inputs, buf, br0::make_index_sequence<ItemCount>{});
    }

    template <bool vertical, typename Tuple>
    template <std::size_t... Is>
    auto Canvas<vertical, Tuple>::DispatchItem(const u8 inputs, u8*& buf, br0::index_sequence<Is...>) -> void {
        ((selectedItem == Is
              ? static_cast<br0::tuple_element_t<Is, Tuple>*>(static_cast<void**>(buff)[Is])->Pass(inputs, buf)
              : void())
         , ...);
    }
}
