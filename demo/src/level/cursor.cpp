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
    while (amt > 0) {
        if (++progress >= HunkLengths[offset]) {
            offset++;
            progress = 0;
        }
        --amt;
    }
    while (amt < 0) {
        if (progress == 0) {
            // Floor at the level start: there is no metatile before run 0,
            // column 0.  Without this, BuildPrevColumn's trailing Move(-1)
            // after reading metatile 0 (pre-revealing the leftmost column at
            // cameraX==16) wraps offset 0 -> 0xFFFF, poisoning the cursor so
            // every later Fetch reads TileData OOB -> garbage tile blocks
            // streamed across the screen on subsequent scrolling.
            if (offset == 0) return;
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
