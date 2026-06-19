#include "collision_map.hpp"

#include "levels.hpp"                  // levelHeight, kHudRows
#include "../graphics/metatiles.hpp"   // GetMetatileCollisions, MetatileCollision
#include <platform-nes/video.hpp>      // TEMP profiler: ppu::SetColorPriority

using namespace br0::intsh;

namespace demo::level {

// Composite-metatile window.  ViewMap[slot*levelHeight + row] holds the metatile
// occupying that cell (dynamic-over-static).  ColMapBaseCol is the leftmost world
// column held; ColMapOrigin is the ring slot it lives in.  A world column maps to
// a slot base-relative: slot = (origin + (col-base)) wrapped once.
// Pinned to absolute .bss (NES): page zero is at the cliff over the FamiStudio
// enclave (its ZEROPAGE segment is placed right after the C-side zero page, so a
// few stray bytes here push FamiStudio's vars past $FF -> ld65 range error).  The
// window is read often but BaseCol/Origin/the cursors are cheap absolute loads;
// none of this state is hot enough to justify spending the last scarce ZP bytes.
#ifdef TARGET_NES
#define colmap_cold __attribute__((section(".bss")))
#else
#define colmap_cold
#endif

colmap_cold u8  ViewMap[kColMapWidth * levelHeight];
colmap_cold u16 ColMapBaseCol;
colmap_cold u8  ColMapOrigin;

// Producer edge cursors, internal to this TU.  Invariant: the *Left cursors sit on
// the row-0 cell of the leftmost held column (ColMapBaseCol) and the *Right cursors
// on the row-0 cell of the rightmost held column (ColMapBaseCol + kColMapWidth - 1),
// for BOTH planes.  ColMapTrack walks the four in lockstep with the window's base so
// a slide only hops one edge one column to reach the entering column -- no walk back
// from the level start.  The static pair reads TileData; the dynamic pair reads the
// coin plane.  Compositing the two here gives every consumer the visible metatile.
namespace {
    colmap_cold Cursor        colLeftStat;
    colmap_cold Cursor        colRightStat;
    colmap_cold DynamicCursor colLeftDyn;
    colmap_cold DynamicCursor colRightDyn;

    // The collector's PERSISTENT cursor pair.  Unlike the four edge cursors above
    // (which track the window), this pair tracks the PLAYER: it parks on the player
    // AABB's top-left dynamic/static cell and is only ever nudged by the player's
    // per-frame travel (<= ~1 cell).  It is seeded once at level load and never reset
    // or rebuilt -- "does not die" -- so the cost of reaching a collected coin is the
    // tiny AABB-local delta from this anchor, NOT a walk from the window edge that grows
    // with how far the player sits inside the window.  colPlayerDyn is the cursor whose
    // offset (Run()) directly indexes DynData to blank a coin; colPlayerStat is its
    // lockstep static partner, read only to reveal the metatile under a removed coin.
    // colPlayerCell is the column-major linear cell index (col*levelHeight + row) the
    // pair currently sits on -- the delta reference for the per-frame re-anchor.
    colmap_cold Cursor        colPlayerStat;
    colmap_cold DynamicCursor colPlayerDyn;
    colmap_cold u16           colPlayerCell;
}

// Stamp one column's composite metatiles into ring slot `slot`.  `stat`/`dyn` are
// parked on the column's row-0 cell; we walk both down levelHeight cells in lockstep
// (column-major, so Move(1) steps one row) and write the dynamic tile when non-air,
// else the static tile -- identical compositing to the render path's GetNextMetaTile.
// The caller's cursor copies are consumed (passed by value).
// noinline: this 2KB-ish body is reached from THREE sites (the seed loop and both
// slides); inlining it -- and through it ColMapTrack -- duplicated the whole column
// walk per call site and blew up PRG.  It runs at most a couple times per frame on
// column entry, so a JSR is free here.  unroll(disable): the levelHeight loop walks
// two RLE cursors; -O3 unrolled it 14x (~150 B/iter of 16-bit-indexed cursor code),
// which is the bulk of the bloat -- the rolled loop is a fraction of the size and
// the per-iter overhead is nothing next to the Fetch/Move work.
__attribute__((noinline))
void ColMapStamp(const u8 slot, Cursor stat, DynamicCursor dyn) {
    u8* col = &ViewMap[slot * levelHeight];

    // Inline both cursor walks instead of calling Move(1) per row.  A Move(1) reloads the
    // cursor's offset/progress, rebuilds the lengths pointer, runs one compare, then writes
    // the state back -- ~50-60 cyc of fixed prologue/epilogue wrapped around a single step, x28
    // (two cursors x 14 rows) every window slide.  Here we hoist all of that once: a data
    // pointer (the metatile to Fetch) and a length pointer (the run-length to compare) per
    // plane, both advanced in lockstep with progress, walking levelHeight consecutive cells
    // forward.  The cursor copies are by-value and discarded, so nothing is written back.
    // Equivalent to running Cursor::Move(1)/DynamicCursor::Move(1) 14 times, minus the churn.
    const u8* sdat = stat.base + stat.offset;   // static metatile (Fetch = *(base+offset))
    const u8* slen = HunkLengths + stat.offset; // static run-length under the cursor
    u8        sp   = stat.progress;
    const u8* ddat = DynData + dyn.offset;      // dynamic metatile (Fetch = DynData[offset])
    const u8* dlen = DynLengths + dyn.offset;   // dynamic run-length under the cursor
    u8        dp   = dyn.progress;

    #pragma clang loop unroll(disable)
    for (u8 r = 0; r < levelHeight; r++) {
        const u8 d = *ddat;
        col[r] = d ? d : *sdat;                 // dynamic-over-static
        if (++sp >= *slen) { ++slen; ++sdat; sp = 0; }   // advance static one cell
        if (++dp >= *dlen) { ++dlen; ++ddat; dp = 0; }   // advance dynamic one cell
    }
}

// Seed the whole window from a single static + dynamic Cursor parked on (leftCol,
// row 0).  Origin starts at 0 so column (leftCol+i) lands in slot i; between columns
// the master cursors cross one column (Move(levelHeight)) so each ColMapStamp copy
// begins on its own row-0 cell.
void ColMapSeed(const u16 leftCol, Cursor stat, DynamicCursor dyn) {
    ColMapBaseCol = leftCol;
    ColMapOrigin  = 0;
    colLeftStat   = stat;          // leftmost held column (base), row 0
    colLeftDyn    = dyn;
    colPlayerStat = stat;          // collector anchor: parked on (leftCol, row 0) now;
    colPlayerDyn  = dyn;           //   the first CollectCoins re-anchors it to the player
    colPlayerCell = static_cast<u16>(leftCol) * levelHeight;
    const auto width = ColMapWidth();
    for (u8 i = 0; i < width; i++) {
        if (i == width - 1) {          // rightmost held, row 0
            colRightStat = stat;
            colRightDyn  = dyn;
        }
        ColMapStamp(i, stat, dyn);     // stamps column i into slot i (copies consumed)
        stat.Move(levelHeight);        // advance masters to the next column's row 0
        dyn.Move(levelHeight);
    }
}

// Lazily centre the window on the camera.  Desired base = (camLeftCol - 4) so the
// 24-wide window holds 4 columns left + viewport(16) + 4 right, clamped so it never
// runs off either end of the level.  Each loop body slides one column and walks BOTH
// edge cursor pairs the same direction, preserving their [base, base+width-1] invariant.
// In steady scroll the camera moves <= 1 column/frame, so a call does 0 or 1 slides.
// noinline: keep this driver's frame (the Cursor::Move walks) out of the giant
// PlayerUpdate ZP frame -- the page-zero-cliff defence the window read relies on.
__attribute__((noinline))
void ColMapTrack(const u16 camLeftCol) {
    i16 desired = static_cast<i16>(camLeftCol) - 4;
    if (desired < 0) desired = 0;
    i16 hi = static_cast<i16>(nColumns) - static_cast<i16>(ColMapWidth());
    if (hi < 0) hi = 0;                       // level narrower than the window
    if (desired > hi) desired = hi;

    while (static_cast<i16>(ColMapBaseCol) < desired) {   // window slides right
        colRightStat.Move(levelHeight);                   // current edges -> entering col
        colRightDyn.Move(levelHeight);
        ColMapSlideRight(colRightStat, colRightDyn);      // base++ inside
        colLeftStat.Move(levelHeight);                    // re-anchor on the new base
        colLeftDyn.Move(levelHeight);
    }
    while (static_cast<i16>(ColMapBaseCol) > desired) {   // window slides left
        colLeftStat.Move(-levelHeight);                   // current edges -> entering col
        colLeftDyn.Move(-levelHeight);
        ColMapSlideLeft(colLeftStat, colLeftDyn);         // base-- inside
        colRightStat.Move(-levelHeight);                  // re-anchor on the new right
        colRightDyn.Move(-levelHeight);
    }
}

// Slide the window one column right: the slot that held the dropped leftmost column
// (ColMapOrigin) is reused for the admitted rightmost column (base+width).  Stamp the
// new column there, then advance base + rotate origin forward one slot.
void ColMapSlideRight(Cursor newRightStat, DynamicCursor newRightDyn) {
    ColMapStamp(ColMapOrigin, newRightStat, newRightDyn);
    ColMapBaseCol++;
    if (++ColMapOrigin >= ColMapWidth()) ColMapOrigin = 0;
}

// Mirror: drop the rightmost column, admit (base-1) on the left.  The freed slot is
// one before the current origin (where the old rightmost lived); rotate origin back
// onto it, drop base, and stamp the new leftmost column into it.
void ColMapSlideLeft(Cursor newLeftStat, DynamicCursor newLeftDyn) {
    ColMapBaseCol--;
    if (ColMapOrigin == 0) ColMapOrigin = ColMapWidth() - 1;
    else                   ColMapOrigin--;
    ColMapStamp(ColMapOrigin, newLeftStat, newLeftDyn);
}

// Resolve world column `col` to its levelHeight composite metatiles in the ring,
// or nullptr when the column is outside the held window.  Identical slot math to
// Blocked's inner resolve (subtract base, one non-pow2 wrap); shared so the render
// path reads the SAME decompressed column the window already produced.
const u8* ColMapColumn(const u16 col) {
    const u16 d = col - ColMapBaseCol;
    if (d >= ColMapWidth()) return nullptr;       // outside window = air (caller skips)
    u8 slot = ColMapOrigin + static_cast<u8>(d);
    if (slot >= ColMapWidth()) slot -= ColMapWidth();
    return &ViewMap[slot * levelHeight];
}

// AABB block test on world-pixel inputs.  The HUD strip is removed so the row index
// lands in level space (matching how ColMapStamp packed row 0 at the top of the level
// data).  We resolve the ring slot ONCE per swept column (a subtract + one wrap), then
// the inner row loop is a flat index + a one-mask collision-class decode -- no RLE, no
// variable shift.  `collectBlocks` picks the audience: the player (true) bonks on coins
// (Collect) as well as walls (Solid); enemies (false) only stop on Solid, passing coins.
// Columns outside the held window read as air, so the sweep needs no X region clamp.
bool Blocked(const u16 px, const u16 py, const u8 w, const u8 h, const bool collectBlocks) {
    i16 rowTop = static_cast<i16>(py >> 4) - kHudRows;
    i16 rowBot = static_cast<i16>((py + h - 1) >> 4) - kHudRows;
    if (rowBot < 0 || rowTop >= levelHeight) return false;   // AABB entirely off-field
    if (rowTop < 0)            rowTop = 0;
    if (rowBot >= levelHeight) rowBot = levelHeight - 1;

    const u16 colL = px >> 4;
    const u16 colR = (px + w - 1) >> 4;
    for (u16 c = colL; c <= colR; c++) {
        const u16 d = c - ColMapBaseCol;
        if (d >= ColMapWidth()) continue;       // column outside window = air
        u8 slot = ColMapOrigin + static_cast<u8>(d);
        if (slot >= ColMapWidth()) slot -= ColMapWidth();   // non-pow2 ring: one wrap
        const u8* colp = &ViewMap[slot * levelHeight];
        for (i16 r = rowTop; r <= rowBot; r++) {
            const MetatileCollision cls = GetMetatileCollisions(colp[r]);
            if (cls == MetatileCollision::Solid) return true;
            if (collectBlocks && cls == MetatileCollision::Collect) return true;
        }
    }
    return false;
}

// Collect coins overlapping the AABB.  Same column/row projection as Blocked.  The
// cost of reaching a coin's run no longer grows with depth into the level: instead of
// walking from the window's left edge (delta = (col-base)*levelHeight + row, which is
// large when the player sits deep inside the window), we keep a PERSISTENT collector
// pair (colPlayerDyn/colPlayerStat) parked on the player.  Once per call we re-anchor
// it to the AABB's top-left cell (colL, rowTop) with a single Move by the player's
// per-frame travel (<= ~1 cell).  From that anchor we copy ONE probe pair and walk it
// MONOTONICALLY through the AABB cells in column-major order -- Move(1) down a column,
// Move(levelHeight-rows) across to the next column's top -- so the run stream is stepped
// ONCE end to end (total ~= the AABB span), never re-walked from the anchor per coin.
// The anchor is updated every call (even when nothing is picked up) so it never drifts
// from the player and never has to be rebuilt.  Blank the run in DynData (permanent:
// any later re-stamp of this column composites air) and overwrite the window cell with
// the reveal (immediate: collision + render see static).
u8 CollectCoins(const u16 px, const u16 py, const u8 w, const u8 h,
                CoinPick* out, const u8 maxOut) {
    i16 rowTop = static_cast<i16>(py >> 4) - kHudRows;
    i16 rowBot = static_cast<i16>((py + h - 1) >> 4) - kHudRows;
    if (rowBot < 0 || rowTop >= levelHeight) return 0;
    if (rowTop < 0)            rowTop = 0;
    if (rowBot >= levelHeight) rowBot = levelHeight - 1;

    const u16 colL = px >> 4;
    const u16 colR = (px + w - 1) >> 4;

    // Re-anchor the persistent collector pair onto the AABB top-left cell EVERY frame.  This is
    // the whole point of the persistent cursor: step is the player's travel since LAST frame
    // (<= ~1 cell walking, 14 on a column crossing), so this is a tiny RLE nudge.  It must stay
    // unconditional -- gating it behind "a coin is present" lets colPlayerCell freeze across dry
    // frames, and the next collection then pays the WHOLE accumulated drift in a single Move
    // (the player walks ~5 columns under a gap in the coins, then eats 70 steps at once -- the
    // exact one-frame spike that gating produced).  Cheap every frame >> occasional huge spike.
    const u16 anchorCell = static_cast<u16>(colL) * levelHeight + static_cast<u16>(rowTop);
    const i16 step = static_cast<i16>(anchorCell) - static_cast<i16>(colPlayerCell);
    ppu::SetColorPriority(ppu::mask::GREEN);   // TEMP profiler: isolate the re-anchor Move cost
    colPlayerDyn.Move(step);
    colPlayerStat.Move(step);
    ppu::SetColorPriority(0);                  // TEMP profiler: end re-anchor region
    colPlayerCell = anchorCell;

    // Cheap gate: the AABB is empty air/ground on almost every frame, so before the probe WALK
    // scan the already-composited window cells (plain ViewMap reads, no RLE walk) for a coin.
    // The dry case -- the overwhelming majority of frames -- returns here, so the probe walk
    // below NEVER runs unless a coin is actually under the AABB this frame.  (The re-anchor above
    // still runs every frame -- it must, to keep step tiny -- but it IS tiny, so that is fine.)
    bool anyCoin = false;
    for (u16 c = colL; c <= colR && !anyCoin; c++) {
        const u16 d = c - ColMapBaseCol;
        if (d >= ColMapWidth()) continue;
        u8 slot = ColMapOrigin + static_cast<u8>(d);
        if (slot >= ColMapWidth()) slot -= ColMapWidth();
        const u8* colp = &ViewMap[slot * levelHeight];
        for (i16 r = rowTop; r <= rowBot; r++)
            if (GetMetatileCollisions(colp[r]) == MetatileCollision::Collect) { anyCoin = true; break; }
    }
    if (!anyCoin) return 0;

    // ONE probe pair copied off the persistent anchor, walked monotonically column-major
    // through the AABB.  pd/ps sit on cell (c, r) at each step, so a coin is removed with a
    // bare pd.Run()/ps.Fetch() -- no per-coin re-walk from the anchor (that quadratic re-walk
    // was the cost).  When a column is outside the window we still step the probe across it
    // (no read) to keep it aligned to the cells.
    DynamicCursor pd   = colPlayerDyn;
    Cursor        ps   = colPlayerStat;
    const i16     rows = rowBot - rowTop;       // 0-based row span of the AABB
    u8 n = 0;
    for (u16 c = colL; ; c++) {
        const u16  d     = c - ColMapBaseCol;
        const bool inWin = d < ColMapWidth();   // column outside window = air
        u8* colp = nullptr;
        if (inWin) {
            u8 slot = ColMapOrigin + static_cast<u8>(d);
            if (slot >= ColMapWidth()) slot -= ColMapWidth();
            colp = &ViewMap[slot * levelHeight];
        }
        for (i16 r = rowTop; r <= rowBot; r++) {
            if (inWin && GetMetatileCollisions(colp[r]) == MetatileCollision::Collect) {
                DynData[pd.Run()] = 0;          // permanent removal (length-1 coin run)
                const u8 reveal = ps.Fetch();   // static metatile to expose
                colp[r] = reveal;               // window now shows the static cell
                if (n < maxOut) out[n] = { c, static_cast<u8>(r), reveal };
                n++;
            }
            if (r != rowBot) { pd.Move(1); ps.Move(1); }   // step down to the next row
        }
        if (c == colR) break;
        pd.Move(levelHeight - rows);            // cross to the next column's top row
        ps.Move(levelHeight - rows);
    }
    return n;
}

}   // namespace demo::level
