#pragma once

#include <intsh>

using namespace br0::intsh;

namespace demo::level {
    struct Level {
        const u8* TileData;
        const u8* HunkLengths;
    };

    template <typename ct, typename ft>
    struct world_space_t {
        ct coarse;
        ft  fine;

        constexpr world_space_t operator+(const world_space_t& r) const {
            const ct f = static_cast<ct>(fine) + r.fine;
            return { static_cast<ct>(coarse + r.coarse + (f >> 8)),
                     static_cast<ft>(f) };
        }

        constexpr world_space_t operator-(const world_space_t& r) const {
            const ct f = static_cast<ct>(fine) - r.fine;
            return { static_cast<ct>(coarse - r.coarse - (f < 0)),
                     static_cast<ft>(f) };
        }

        // cross-instantiation conversion (e.g. WorldSpace <-> DeltaWorldSpace)
        template <typename c2, typename f2>
        constexpr operator world_space_t<c2, f2>() const {
            return { static_cast<c2>(coarse), static_cast<f2>(fine) };
        }
    };

    typedef world_space_t<u16, u8> WorldSpace;
    typedef world_space_t<i16, i8> DeltaWorldSpace;
}