#pragma once

#include <intsh>

using namespace br0::intsh;

namespace demo::level {
    struct Level {
        const u8* TileData;     // static plane: flat column-major ROM array
        u16       nColumns;     // level width in metatile columns
        const u8* DynData;      // dynamic plane: RLE run-data (ROM, copied to RAM at load)
        const u8* DynLengths;   // dynamic plane: RLE run-lengths (ROM)
        u16       DynRuns;      // dynamic-plane run count to copy into the RAM pool
    };
}
