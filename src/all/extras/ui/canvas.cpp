#include <platform-nes/extras/ui/canvas.hpp>

#include "platform-nes/input.hpp"

namespace ui {
    template <bool vertical, typename Tuple>
    Canvas<vertical, Tuple>::Canvas(void* buff, const u8 confirmButton) : buff(buff), confirmButton(confirmButton) {

    }

    template <bool vertical, typename Tuple>
    auto Canvas<vertical, Tuple>::Pass(const u8 inputs) -> void {
        if constexpr (vertical) {
            if (inputs & (input::UP | input::DOWN)) {
                // move selectedButton
                return;
            }
        } else {
            if (inputs & (input::LEFT | input::RIGHT)) {
                // move selectedButton
                return;
            }
        }

        DispatchItem(inputs, br0::make_index_sequence<ItemCount>{});
    }

    template <bool vertical, typename Tuple>
    template <std::size_t... Is>
    auto Canvas<vertical, Tuple>::DispatchItem(const u8 inputs, br0::index_sequence<Is...>) -> void {
        ((selectedItem == Is
              ? static_cast<br0::tuple_element_t<Is, Tuple>*>(static_cast<void**>(buff)[Is])->Pass(inputs)
              : void())
         , ...);
    }
}
