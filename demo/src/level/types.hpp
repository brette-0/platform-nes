#pragma once

#include <intsh>

using namespace br0::intsh;

namespace demo::level {
    struct Level {
        const u8* TileData;     // static plane: RLE run-data (ROM, fetched in place)
        const u8* HunkLengths;  // static plane: RLE run-lengths (ROM)
        const u8* DynData;      // dynamic plane: RLE run-data (ROM, copied to RAM at load)
        const u8* DynLengths;   // dynamic plane: RLE run-lengths (ROM)
        u8        DynRuns;      // dynamic-plane run count to copy into the RAM pool
    };
}