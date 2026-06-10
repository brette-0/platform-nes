#pragma once

template <typename t>
concept addable = requires (t a, t b) {
    a + b;
    a - b;
};

template <addable t>
struct vec2 {
    t x;
    t y;

    constexpr vec2 operator+(const vec2& rhs) const {
        return { static_cast<t>(x + rhs.x), static_cast<t>(y + rhs.y) };
    }

    constexpr vec2 operator-(const vec2& rhs) const {
        return { static_cast<t>(x - rhs.x), static_cast<t>(y - rhs.y) };
    }

    constexpr vec2& operator+=(const vec2& rhs) {
        x += rhs.x;
        y += rhs.y;
        return *this;
    }
    constexpr vec2& operator-=(const vec2& rhs) {
        x -= rhs.x;
        y -= rhs.y;
        return *this;
    }

    // element-wise conversion to a vec2 of another (convertible) element type
    template <addable u>
    constexpr operator vec2<u>() const {
        return { static_cast<u>(x), static_cast<u>(y) };
    }
};
