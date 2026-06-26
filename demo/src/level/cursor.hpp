#pragma once

#include <intsh>

using namespace br0::intsh;

namespace demo::level {
    // Static-plane cursor.  The static layer is stored as a flat column-major
    // array in ROM; no RLE, no run tracking.  Move/Seek are pure pointer
    // arithmetic and Fetch is a single dereference -- all free on 6502.
    class Cursor {
    public:
        const u8* dp;   // direct pointer into flat TileData

        void Move(const i8  amt) { dp += amt; }
        void Seek(const i16 amt) { dp += amt; }

        [[nodiscard]] u8 Fetch() const { return *dp; }
    };
}
