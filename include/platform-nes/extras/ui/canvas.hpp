#pragma once

#include <intsh>
#include <br0/tuple>

using namespace br0::intsh;

namespace ui {
    template <bool vertical, typename Tuple>
    class Canvas {
    public:
        Canvas(void* buff, u8 confirmButton);
        auto Pass(u8 inputs) -> void;

    private:
        static constexpr std::size_t ItemCount = br0::tuple_size_v<Tuple>;

        template <std::size_t... Is>
        auto DispatchItem(u8 inputs, br0::index_sequence<Is...>) -> void;

        void* buff;
        u8 selectedItem;
        const u8 confirmButton;
    };
}