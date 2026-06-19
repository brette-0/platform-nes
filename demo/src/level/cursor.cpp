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
    // Hoist offset/progress into locals for the duration of the walk.  Mutating the
    // members directly forces llvm-mos to round-trip them through the `this` pointer on
    // EVERY iteration (a ~200-cycle indirect load/store per metatile stepped); held in
    // locals the allocator keeps them in zero-page registers and writes back once at the
    // end -- the run-length compare becomes the only real per-step cost.  This is the hot
    // primitive behind ColMapTrack/Stamp and the player re-anchor, so it pays everywhere.
    u8  p = progress;
    // Running pointer into the run-length table instead of re-indexing HunkLengths[o] each
    // step.  HunkLengths[o] compiled to clc/adc/adc -- a full 16-bit base+offset reconstruction
    // EVERY metatile stepped; lp walks alongside the run index (inc/inc on a crossing only) so
    // the per-step cost collapses to the bare run-length compare lda (lp),y.  o is recovered
    // from lp once at the end.
    const u8* lp = HunkLengths + offset;
    // unroll(disable): callers pass a CONSTANT step (Move(1) per row, Move(levelHeight)
    // to cross a column).  -O3 fully unrolled the constant-count instances -- a single
    // inlined Move(14) became ~14 copies of this 16-bit-indexed run-walk, and ColMapTrack
    // (8 such crossings) ballooned to ~8KB of PRG.  Rolled, each Move is a tight loop; the
    // branch overhead per metatile is nothing beside the run-length compare it guards.
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
            // Floor at the level start: there is no metatile before run 0,
            // column 0.  Without this, BuildPrevColumn's trailing Move(-1)
            // after reading metatile 0 (pre-revealing the leftmost column at
            // cameraX==16) wraps offset 0 -> 0xFFFF, poisoning the cursor so
            // every later Fetch reads TileData OOB -> garbage tile blocks
            // streamed across the screen on subsequent scrolling.
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
