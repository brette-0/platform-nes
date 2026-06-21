#include "cursor.hpp"
#include "levels.hpp"

using namespace demo::level;

// Relative reposition: walk the RLE by `amt` from the cursor's current
// run/progress (keeps offset).  Same engine as Move(); used on the off-field
// path, where the actor pins to the field edge and no collision test is run.
void Cursor::Seek(const i16 amt) {
    Move(amt);
}

// Walk the RLE stream by `amt` metatiles (forward if positive, backward if
// negative), crossing run boundaries as needed.  `offset` indexes the current
// run; `progress` is how far into that run we are (0 .. HunkLengths[offset]-1).
void Cursor::Move(i16 amt) {
    u8  p = progress;
    const u8* lp = HunkLengths + offset;
    #pragma clang loop unroll(disable)
    while (amt > 0) {
        if (++p >= *lp) {
            ++lp;
            p = 0;
        }
        --amt;
    }
    #pragma clang loop unroll(disable)
    while (amt < 0) {
        if (p == 0) {
            if (lp == HunkLengths) break;   // already at the floor (o==0, p==0)
            --lp;
            p = *lp;
        }
        --p;
        ++amt;
    }
    offset = static_cast<u16>(lp - HunkLengths);
    progress = p;
}

u8 Cursor::Fetch() const {
    return *(base + offset);
}
