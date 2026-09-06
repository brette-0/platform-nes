#pragma once

#include <platform-nes/extras/math.hpp>

#include <intsh>
#include <type_traits>
#include <limits>

#include "platform-nes/technology.hpp"
using namespace br0::intsh;

template <typename T>
constexpr auto MINSIZE bcd(T hex) -> std::make_unsigned_t<T> {
    using U = std::make_unsigned_t<T>;
    constexpr auto bits = std::numeric_limits<U>::digits;

    U value = abs(hex);
    U digits = 0;

    for (auto i = 0; i < bits; ++i) {
        for (auto shift = 0; shift < bits; shift += 4) {
            const U nibble = static_cast<U>(digits >> shift) & 0xF;
            if (nibble >= 5) digits = static_cast<U>(digits + (static_cast<U>(3) << shift));
        }

        const U carry = static_cast<U>(value >> (bits - 1)) & 1;
        value = static_cast<U>(value << 1);
        digits = static_cast<U>((digits << 1) | carry);
    }

    return digits;
}

// delete response after using
template <typename T>
constexpr auto MINSIZE bcdtext(const T hex, const u8* conversionTable) -> char* {
    using U = std::make_unsigned_t<T>;
    constexpr auto bits = std::numeric_limits<U>::digits;
    constexpr unsigned nibbles = bits / 4;
    constexpr unsigned signOffset = std::is_signed_v<T> ? 1 : 0;

    char* response = new char[nibbles + signOffset];

    U dec = bcd(hex);
    for (unsigned i = 0; i < nibbles; ++i) {
        const u8 nibble = dec & 0xF;
        dec = static_cast<U>(dec >> 4);
        response[signOffset + nibbles - 1 - i] = static_cast<char>(conversionTable[nibble]);
    }

    if constexpr (std::is_signed_v<T>) {
        response[0] = hex < 0 ? '-' : ' ';
    }

    return response;
}