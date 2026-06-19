#pragma once

#include <intsh>

using namespace br0::intsh;

namespace demo::level {
    class Cursor {
    public:
        u16 offset;
        u8  progress;
        const u8* base;

        void Seek(i16 amt);  // relative reposition (keeps offset); same walk as Move
        void Move(i16 amt);
        [[nodiscard]] u8   Fetch() const;
    };
}