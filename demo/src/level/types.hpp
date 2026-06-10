#pragma once

#include <intsh>

using namespace br0::intsh;

namespace demo::level {
    struct Level {
        const u8* TileData;
        const u8* HunkLengths;
    };
}