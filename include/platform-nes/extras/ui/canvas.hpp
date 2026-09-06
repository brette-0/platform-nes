#pragma once

#include <intsh>
#include <br0/tuple>
#include <type_traits>
#include <platform-nes/extras/ui/tickbox.hpp>
#include <platform-nes/extras/ui/tileslider.hpp>

using namespace br0::intsh;

namespace ui {
    namespace detail {
        // IsOneOf<T, Allowed...>: true iff T is exactly one of the Allowed types.
        template <typename T, typename... Allowed>
        inline constexpr bool IsOneOf = (std::is_same_v<T, Allowed> || ...);

        // TupleWhitelisted<ItemTuple, WhitelistTuple>: true iff every type in
        // ItemTuple appears in WhitelistTuple. Pure compile-time type check,
        // costs nothing at runtime.
        template <typename ItemTuple, typename WhitelistTuple>
        struct TupleWhitelisted;

        template <typename... Items, typename... Allowed>
        struct TupleWhitelisted<br0::tuple<Items...>, br0::tuple<Allowed...>> {
            static constexpr bool value = (IsOneOf<Items, Allowed...> && ...);
        };
    }

    // The only types Canvas is allowed to hold in its item Tuple. Pass()
    // reinterprets buff[i] as tuple_element_t<i, Tuple>* with no virtual
    // dispatch or RTTI to check it against, so this whitelist is what stops
    // an unrelated type (e.g. int*) from being smuggled in and cast to a
    // widget pointer at runtime. TileSlider is templated on its own
    // orientation, so both instantiations are listed individually.
    using ItemWhitelist = br0::tuple<
        button::TickBox,
        slider::TileSlider<true>,
        slider::TileSlider<false>
    >;

    template <bool vertical, typename Tuple>
    class Canvas {
        static_assert(detail::TupleWhitelisted<Tuple, ItemWhitelist>::value,
            "ui::Canvas: Tuple contains a type not in ui::ItemWhitelist");

    public:
        Canvas(void* buff, u8 confirmButton);
        // Writes whichever item's op(s) at *buf and advances buf past them,
        // instead of touching the PPU directly. buf must point into a
        // caller-owned buffer with enough room left for whatever the
        // currently selected item may write.
        auto Pass(u8 inputs, u8*& buf) -> void;

    private:
        static constexpr std::size_t ItemCount = br0::tuple_size_v<Tuple>;

        template <std::size_t... Is>
        auto DispatchItem(u8 inputs, u8*& buf, br0::index_sequence<Is...>) -> void;

        void* buff;
        u8 selectedItem;
        const u8 confirmButton;
    };
}