#pragma once
#include <intsh>
#include <type_traits>

namespace demo {

// Absolute value for any signed integer type.
// A template avoids per-type duplicates while the cast back to T preserves
// the narrowest type (important on NES where widening to int costs cycles).
template<typename T>
    requires std::is_signed_v<T>
constexpr T Abs(const T v) { return v < T{0} ? static_cast<T>(-v) : v; }

}   // namespace demo
