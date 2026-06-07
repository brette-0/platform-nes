#include "cursor.hpp"
#include "levels.hpp"

using namespace demo::level;

// Relative reposition: walk the RLE by `amt` from the cursor's current
// run/progress (keeps offset).  Same engine as Move(); used on the off-field
// path, where the actor pins to the field edge and no collision test is run.
void Cursor::Seek(i16 amt) {
    Move(amt);
}

// Walk the RLE stream by `amt` metatiles (forward if positive, backward if
// negative), crossing run boundaries as needed.  `offset` indexes the current
// run; `progress` is how far into that run we are (0 .. HunkLengths[offset]-1).
void Cursor::Move(i16 amt) {
    while (amt > 0) {
        if (++progress >= HunkLengths[offset]) {
            offset++;
            progress = 0;
        }
        --amt;
    }
    while (amt < 0) {
        if (progress == 0) {
            offset--;
            progress = HunkLengths[offset];
        }
        --progress;
        ++amt;
    }
}

u8 Cursor::Fetch() {
    return *(base + offset);
}
