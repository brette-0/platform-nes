#include "collision_map.hpp"

#include "levels.hpp"                  // levelHeight, kHudRows
#include "../../graphics/metatiles.hpp"   // GetMetatileCollisions, MetatileCollision
#include "../../banks.hpp"             // FIXED, LEVEL_CODE

#include <array>

using namespace br0::intsh;

namespace level {

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

// Precomputed slot → ViewMap column-base offsets.  slot * levelHeight requires a
// 16-bit software multiply on 6502 (14 * 23 = 322 > 255, result overflows u8).
// This constexpr ROM table replaces that with a single indexed load, paying ~6
// cycles vs ~80-100 cycles per column resolved in Blocked / ColMapColumn.
static constexpr std::array<u16, kColMapWidth> kSlotOffset = []() constexpr {
    std::array<u16, kColMapWidth> a{};
    for (u8 i = 0; i < kColMapWidth; i++) a[i] = static_cast<u16>(i * levelHeight);
    return a;
}();

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
    // with how far the player sits inside the window.  dyn is the cursor whose offset
    // (Run()) directly indexes DynData to blank a coin; stat is its lockstep static
    // partner, read only to reveal the metatile under a removed coin.  cell is the
    // column-major linear cell index (col*levelHeight + row) the pair currently sits
    // on -- the delta reference for the per-frame re-anchor.  colR/rowB are the last
    // AABB's edges, for the early-exit guard.  dry is set true only once a scan of the
    // CURRENT (unchanged) AABB has come back empty -- the early-exit guard may only skip
    // a stationary-AABB scan when this is true, otherwise a coin left behind by the
    // one-coin-per-call cap (see CollectCoinsImpl) would never get a second look before
    // the player moves on, and would be silently skipped ("phase through"). Cleared
    // whenever the AABB moves to new cells (unverified) or a coin was just collected
    // (there may be another one still sitting in the same AABB).
    //
    // One struct, two instances (P1/P2) rather than two independently-named variable
    // sets: CollectCoins/CollectCoins2 used to be ~2.3 KiB apiece of near-identical
    // logic differing only in which variable set they touched -- see CollectCoinsImpl.
    struct CollectorAnchor {
        Cursor        stat;
        DynamicCursor dyn;
        u16           cell;
        u16           colR;
        u8            rowB;
        bool          dry;
    };
    colmap_cold CollectorAnchor colPlayerAnchor;
    // Independent anchor for player 2. Same invariants as the P1 anchor; keeping them
    // separate means each player tracks their own AABB with a tiny per-frame step
    // rather than bouncing a shared cursor across the inter-player distance every frame.
    colmap_cold CollectorAnchor colPlayer2Anchor;
}

// Stamp one column's composite metatiles into ring slot `slot`.  `stat`/`dyn` are
// parked on the column's row-0 cell; we walk both down levelHeight cells in lockstep
// (column-major, so Move(1) steps one row) and write the dynamic tile when non-air,
// else the static tile -- identical compositing to the render path's GetNextMetaTile.
// The caller's cursor copies are consumed (passed by value).
// noinline: this body is reached from THREE sites (the seed loop and both slides);
// inlining it duplicated the walk per call site and blew up PRG.  It runs at most
// a couple of times per frame on column entry so a JSR is free here.
// unroll(disable): -O3 unrolled the 14-row loop; the rolled form is much smaller
// and the per-iter overhead is negligible next to the dyn-cursor RLE work.
// LEVEL_CODE: paired with banks.hpp's ::level_data_tag (window 2) -- `stat`
// reads the static plane, `dyn` reads the dynamic plane's ROM-resident
// lengths, both out of that window. See banks.hpp's ::level_code_tag.
__attribute__((noinline)) LEVEL_CODE
void ColMapStamp(const u8 slot, Cursor stat, DynamicCursor dyn) {
    u8* col = ViewMap + kSlotOffset[slot];

    // Static is a flat ROM pointer; dynamic still walks RLE.
    const u8* sdat = stat.dp;
    const u8* ddat = dyn.dp;
    const u8* dlen = dyn.lp;
    u8        dp   = dyn.progress;

    #pragma clang loop unroll(disable)
    for (u8 r = 0; r < levelHeight; r++) {
        const u8 d = *ddat;
        col[r] = d ? d : *sdat;                 // dynamic-over-static
        ++sdat;                                  // flat: one byte per cell
        if (++dp >= *dlen) { ++dlen; ++ddat; dp = 0; }
    }
}

// Seed the whole window from a single static + dynamic Cursor parked on (leftCol,
// row 0).  Origin starts at 0 so column (leftCol+i) lands in slot i; between columns
// the master cursors cross one column (Move(levelHeight)) so each ColMapStamp copy
// begins on its own row-0 cell.
// LEVEL_CODE: called from EnterLevelSetup (::COLD) at level load, wrapped in an
// explicit mmc3::CallInBlock<level_code_tag> there since COLD can't reach this
// bank by a plain call -- see that call site's own comment in level.cpp.
// noinline: now called from exactly ONE site (that same wrapping lambda), so
// without it LTO inlines the whole body -- including its ColMapStamp calls --
// into the lambda, which is compiled as part of the FIXED trampoline. Same
// failure mode as LoadLevel's own noinline; see its comment.
__attribute__((noinline)) LEVEL_CODE
void ColMapSeed(const u16 leftCol, Cursor stat, DynamicCursor dyn) {
    ColMapBaseCol = leftCol;
    ColMapOrigin  = 0;
    colLeftStat   = stat;          // leftmost held column (base), row 0
    colLeftDyn    = dyn;
    colPlayerAnchor.stat = stat;   // collector anchor: parked on (leftCol, row 0) now;
    colPlayerAnchor.dyn  = dyn;    //   the first CollectCoins re-anchors it to the player
    colPlayerAnchor.cell = static_cast<u16>(leftCol) * levelHeight;
    colPlayerAnchor.colR = leftCol;
    colPlayerAnchor.rowB = 0;
    colPlayerAnchor.dry  = false;  // unverified: first call for this AABB must scan
    colPlayer2Anchor = colPlayerAnchor;   // P2 anchor seeded identically; diverges as P2 moves
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

// Pair advance helpers: static cursor is a flat pointer (just add/subtract
// levelHeight); only the dynamic cursor still needs RLE walking.
// always_inline: both callers are in this TU; the compiler sees the concrete
// global addresses and emits absolute addressing instead of the indirect ZP
// addressing that noinline + reference parameters forced.
AI
static void AdvancePairForward(Cursor& s, DynamicCursor& d) {
    s.dp += levelHeight;
    const u8* dlp  = d.lp;  u8* ddp = d.dp;  u8 dprog = d.progress;
    #pragma clang loop unroll(disable)
    for (u8 i = levelHeight; i != 0; --i) {
        if (++dprog >= *dlp) { ++dlp; ++ddp; dprog = 0; }
    }
    d.lp = dlp; d.dp = ddp; d.progress = dprog;
}

AI
static void AdvancePairBackward(Cursor& s, DynamicCursor& d) {
    s.dp -= levelHeight;
    const u8* dlp  = d.lp;  u8* ddp = d.dp;  u8 dprog = d.progress;
    #pragma clang loop unroll(disable)
    for (u8 i = levelHeight; i != 0; --i) {
        if (dprog == 0) {
            if (dlp == DynLengths) break;
            --dlp; --ddp; dprog = *dlp;
        }
        --dprog;
    }
    d.lp = dlp; d.dp = ddp; d.progress = dprog;
}

// Lazily centre the window on the camera.  Desired base = (camLeftCol - 4) so the
// 24-wide window holds 4 columns left + viewport(16) + 4 right, clamped so it never
// runs off either end of the level.  Each loop body slides one column and walks BOTH
// edge cursor pairs the same direction, preserving their [base, base+width-1] invariant.
// In steady scroll the camera moves <= 1 column/frame, so a call does 0 or 1 slides.
// noinline: keep this driver's frame out of the giant PlayerUpdate ZP frame.
// LEVEL_CODE: called only from level::main()'s own loop. See banks.hpp's
// ::level_code_tag.
__attribute__((noinline)) LEVEL_CODE
void ColMapTrack(const u16 camLeftCol) {
    i16 desired = static_cast<i16>(camLeftCol) - 4;
    if (desired < 0) desired = 0;
    i16 hi = static_cast<i16>(nColumns) - static_cast<i16>(ColMapWidth());
    if (hi < 0) hi = 0;                       // level narrower than the window
    if (desired > hi) desired = hi;

    u8 colIter = 0;
    while (static_cast<i16>(ColMapBaseCol) < desired) {   // window slides right
#ifdef TARGET_NES
        ppu::SetColorPriority(static_cast<u8>((++colIter << 5) & 0xE0));
#endif
        AdvancePairForward(colRightStat, colRightDyn);    // entering column
        ColMapSlideRight(colRightStat, colRightDyn);      // base++
        AdvancePairForward(colLeftStat, colLeftDyn);      // re-anchor left edge
    }
    while (static_cast<i16>(ColMapBaseCol) > desired) {   // window slides left
#ifdef TARGET_NES
        ppu::SetColorPriority(static_cast<u8>((++colIter << 5) & 0xE0));
#endif
        AdvancePairBackward(colLeftStat, colLeftDyn);     // entering column
        ColMapSlideLeft(colLeftStat, colLeftDyn);         // base--
        AdvancePairBackward(colRightStat, colRightDyn);   // re-anchor right edge
    }
#ifdef TARGET_NES
    ppu::SetColorPriority(0);
#endif
}

// Slide the window one column right: the slot that held the dropped leftmost column
// (ColMapOrigin) is reused for the admitted rightmost column (base+width).  Stamp the
// new column there, then advance base + rotate origin forward one slot.
// LEVEL_CODE: called only from ColMapTrack.
LEVEL_CODE
void ColMapSlideRight(const Cursor &newRightStat, DynamicCursor newRightDyn) {
    ColMapStamp(ColMapOrigin, newRightStat, newRightDyn);
    ColMapBaseCol++;
    if (++ColMapOrigin >= ColMapWidth()) ColMapOrigin = 0;
}

// Mirror: drop the rightmost column, admit (base-1) on the left.  The freed slot is
// one before the current origin (where the old rightmost lived); rotate origin back
// onto it, drop base, and stamp the new leftmost column into it.
// LEVEL_CODE: called only from ColMapTrack.
LEVEL_CODE
void ColMapSlideLeft(const Cursor &newLeftStat, DynamicCursor newLeftDyn) {
    ColMapBaseCol--;
    if (ColMapOrigin == 0) ColMapOrigin = ColMapWidth() - 1;
    else                   ColMapOrigin--;
    ColMapStamp(ColMapOrigin, newLeftStat, newLeftDyn);
}

// Resolve world column `col` to its levelHeight composite metatiles in the ring,
// or nullptr when the column is outside the held window.  Identical slot math to
// Blocked's inner resolve (subtract base, one non-pow2 wrap); shared so the render
// path reads the SAME decompressed column the window already produced.
// FIXED: sole callers are BuildNextColumn/BuildPrevColumn (levels.cpp), which are
// now ::ACTORS -- grouped with its sibling Blocked (same static ViewMap/ColMapBaseCol/
// ColMapOrigin state, already FIXED) rather than moved into the actors bank itself,
// so both the actor-bank and the level-domain callers can reach it as an ordinary call.
FIXED
const u8* ColMapColumn(const u16 col) {
    const u16 d = col - ColMapBaseCol;
    if (d >= ColMapWidth()) return nullptr;       // outside window = air (caller skips)
    u8 slot = ColMapOrigin + static_cast<u8>(d);
    if (slot >= ColMapWidth()) slot -= ColMapWidth();
    return ViewMap + kSlotOffset[slot];
}

// AABB block test on world-pixel inputs.  The HUD strip is removed so the row index
// lands in level space (matching how ColMapStamp packed row 0 at the top of the level
// data).  We resolve the ring slot ONCE per swept column (a subtract + one wrap), then
// the inner row loop is a flat index + a one-mask collision-class decode -- no RLE, no
// variable shift.  `collectBlocks` picks the audience: the player (true) bonks on coins
// (Collect) as well as walls (Solid); enemies (false) only stop on Solid, passing coins.
// Columns outside the held window read as air, so the sweep needs no X region clamp.
// noinline: called 4x from Player::Update (grounded check + 3x in ProcessMovement) plus
// from Actor::Move; LTO was duplicating the whole body into every call site instead of
// emitting one JSR target, which is both wasted PRG and unmeasurable -- see this repo's
// actor-bank-isolation discussion. Same rationale as ColMapStamp/ColMapTrack above.
// FIXED: measures 402 bytes standalone -- pulled out of prg_rom_level and into the
// always-mapped fixed bank as a down payment on giving actor/player code (the caller)
// an exclusive bank of its own later; costs nothing today since FIXED is already mapped
// whenever level code runs.
__attribute__((noinline)) FIXED
bool Blocked(const u16 px, const u16 py, const vec2<u8> dimensions, const bool collectBlocks) {
    const u8 w = dimensions.x; const u8 h = dimensions.y;
    i16 rowTop = static_cast<i16>(py >> 4) - kHudRows;
    i16 rowBot = static_cast<i16>((py + h - 1) >> 4) - kHudRows;
    if (rowBot < 0 || rowTop >= levelHeight) return false;   // AABB entirely off-field
    if (rowTop < 0)            rowTop = 0;
    if (rowBot >= levelHeight) rowBot = levelHeight - 1;

    // Clamp to u8 now that bounds are validated -- eliminates i16 indexing below.
    const u8 rowT = static_cast<u8>(rowTop);
    const u8 rowB = static_cast<u8>(rowBot);

    const u16 colL = px >> 4;
    const u16 colR = (px + w - 1) >> 4;
    for (u16 c = colL; c <= colR; c++) {
        const u16 d = c - ColMapBaseCol;
        if (d >= ColMapWidth()) continue;       // column outside window = air
        u8 slot = ColMapOrigin + static_cast<u8>(d);
        if (slot >= ColMapWidth()) slot -= ColMapWidth();   // non-pow2 ring: one wrap
        // kSlotOffset replaces slot*levelHeight (16-bit software multiply) with a
        // ROM table lookup.  Start colp at rowT so the inner walk is a bare *colp.
        const u8* colp = ViewMap + kSlotOffset[slot] + rowT;
        for (u8 cnt = rowB - rowT + 1; cnt != 0; --cnt, ++colp) {
            const MetatileCollision cls = GetMetatileCollisions(*colp);
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
// pair (anchor.dyn/anchor.stat) parked on the player.  Once per call we re-anchor
// it to the AABB's top-left cell (colL, rowTop) with a single Move by the player's
// per-frame travel (<= ~1 cell).  From that anchor we copy ONE probe pair and walk it
// MONOTONICALLY through the AABB cells in column-major order -- Move(1) down a column,
// Move(levelHeight-rows) across to the next column's top -- so the run stream is stepped
// ONCE end to end (total ~= the AABB span), never re-walked from the anchor per coin.
// The anchor is updated every call (even when nothing is picked up) so it never drifts
// from the player and never has to be rebuilt.  Blank the run in DynData (permanent:
// any later re-stamp of this column composites air) and overwrite the window cell with
// the reveal (immediate: collision + render see static).
//
// Collects AT MOST ONE coin per call, even if the AABB straddles several (a dense
// coin layout can pack multiple Collect cells into one 2x2-metatile AABB). This is
// the "one pickup per frame" technique: it bounds the per-frame VRAM-queue burst
// (PushCoinVram queues 4 writes per coin; several coins in one frame can blow the
// queue), and it bounds the CPU burst the same way: the expensive part per coin
// (blank + reveal-fetch + reveal-write + queue push) now happens at most once per
// call instead of once per coin found. A frame is 1/60s -- spreading a multi-coin
// cluster's collection over a few consecutive frames as the player passes through
// it is not perceptible.
//
// Unlike an earlier attempt at this, the "same cell as last frame -> skip the
// walk" guard further down is UNTOUCHED -- removing it outright (on the theory
// that pd.Fetch()!=0 filtering makes it redundant once collection is capped)
// was tried and caused a hang on landing, so it's not an option. But left as a
// pure position check, that guard is WRONG under the one-coin-per-call cap: a
// metatile cell is 16px wide and the player moves at most ~2.5px/frame, so
// "same cell as last frame" is true for many consecutive frames just from
// walking through one cell, not only when standing still. If a coin was left
// behind in the AABB when the cap kicked in, the position-only guard would
// skip re-scanning it for all of those frames and the player would cross out
// of the AABB with it still uncollected ("phase through"). anchor.dry (see
// below) closes that gap: the guard may only skip a same-position scan once
// this cell has actually been VERIFIED empty, not merely because the position
// didn't change -- so a stationary-relative-to-cell AABB keeps getting
// re-scanned, once per frame, for as long as it still has a coin in it.
//
// Shared body for CollectCoins/CollectCoins2 below: the two used to be independent
// ~2.3 KiB copies of this exact logic, differing only in which CollectorAnchor they
// touched. `anchor` is the one piece of per-player state; everything else is common.
// noinline: called once per frame per player from Player::Update; kept out-of-line for
// the same reason as Blocked above -- see that function's comment.
// FIXED: see Blocked's comment.
__attribute__((noinline)) FIXED
static u8 CollectCoinsImpl(CollectorAnchor& anchor, const u16 px, const u16 py,
                            const vec2<u8> dimensions, CoinPick* out, const u8 maxOut) {
    const u8 w = dimensions.x; const u8 h = dimensions.y;
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
    // unconditional -- gating it behind "a coin is present" lets anchor.cell freeze across dry
    // frames, and the next collection then pays the WHOLE accumulated drift in a single Move
    // (the player walks ~5 columns under a gap in the coins, then eats 70 steps at once -- the
    // exact one-frame spike that gating produced).  Cheap every frame >> occasional huge spike.
    const u16 anchorCell = static_cast<u16>(colL) * levelHeight + static_cast<u16>(rowTop);
    const i16 step      = static_cast<i16>(anchorCell) - static_cast<i16>(anchor.cell);
    const u8   rowB     = static_cast<u8>(rowBot);
    const bool sameColR = (colR == anchor.colR);
    const bool sameRowB = (rowB == anchor.rowB);
    anchor.cell = anchorCell;
    anchor.colR = colR;
    anchor.rowB = rowB;
    // Same cells as last frame AND that AABB was already verified empty → skip the walk.
    // Guard all four AABB edges: step covers colL+rowTop; colR and rowB change independently
    // when px%16 or py%16 crosses 0↔1 without the top-left anchor moving. anchor.dry is
    // the piece that keeps this safe under the one-coin-per-call cap: a coin left behind in
    // this same AABB clears it (see below), so a stationary AABB keeps getting re-scanned
    // every frame until it's actually drained, instead of stopping after the first pickup
    // and leaving the rest to "phase through" uncollected once the player moves on.
    if (step == 0 && sameColR && sameRowB && anchor.dry) return 0;
    // After level load step is bounded to ±(levelHeight+1) ≤ 15; use the i8 path
    // (single-byte loop counter on 6502) except on the first call after ColMapSeed
    // where the player may be far from the seed point.
    if (step >= -128 && step <= 127) {
        anchor.dyn.Move(static_cast<i8>(step));
        anchor.stat.Move(static_cast<i8>(step));
    } else {
        anchor.dyn.Seek(step);
        anchor.stat.Seek(step);
    }

    // ViewMap pre-scan: a cheap gate before the cursor probe walk.  ViewMap is composite
    // (dynamic-over-static), so this fires for static Collect tiles too -- but the probe's
    // pd.Fetch()!=0 guard rejects those in one dereference.  The gain is that the probe walk
    // (cursor copies + Move calls) is skipped entirely on every frame where no Collect tile
    // sits in the AABB, which is nearly every frame in open terrain.  The seek above must still
    // run unconditionally to keep the persistent cursor aligned.
    {
        bool anyCoin = false;
        for (u16 c = colL; !anyCoin && c <= colR; c++) {
            const u16 d = c - ColMapBaseCol;
            if (d >= ColMapWidth()) continue;
            u8 slot = ColMapOrigin + static_cast<u8>(d);
            if (slot >= ColMapWidth()) slot -= ColMapWidth();
            const u8* colp = ViewMap + kSlotOffset[slot];
            for (i16 r = rowTop; !anyCoin && r <= rowBot; r++) {
                if (GetMetatileCollisions(colp[r]) == MetatileCollision::Collect)
                    anyCoin = true;
            }
        }
        if (!anyCoin) { anchor.dry = true; return 0; }   // verified empty: safe to skip next time
    }

    // ONE probe pair copied off the persistent anchor, walked monotonically column-major
    // through the AABB.  pd/ps sit on cell (c, r) at each step, so a coin is removed with a
    // bare *pd.dp write /ps.Fetch() -- no per-coin re-walk from the anchor (that quadratic
    // re-walk was the cost).  When a column is outside the window we still step the probe
    // across it (no read) to keep it aligned to the cells. Stops at the FIRST coin found --
    // see the one-coin-per-call note above.
    DynamicCursor pd   = anchor.dyn;
    Cursor        ps   = anchor.stat;
    const i16     rows = rowBot - rowTop;       // 0-based row span of the AABB
    u8 n = 0;
    for (u16 c = colL; ; c++) {
        const u16  d     = c - ColMapBaseCol;
        const bool inWin = d < ColMapWidth();   // column outside window = air
        u8* colp = nullptr;
        if (inWin) {
            u8 slot = ColMapOrigin + static_cast<u8>(d);
            if (slot >= ColMapWidth()) slot -= ColMapWidth();
            colp = ViewMap + kSlotOffset[slot];
        }
        for (i16 r = rowTop; r <= rowBot; r++) {
            if (inWin && pd.Fetch() != 0 && GetMetatileCollisions(colp[r]) == MetatileCollision::Collect) {
                // *pd.dp = 0 is DynData[pd.Run()] = 0 (Run() is just dp-DynData)
                // without the round trip: pd.dp already points at that DynData
                // byte, so this skips a 16-bit subtract (Run()) immediately
                // followed by a 16-bit re-add (indexing back into DynData) --
                // 64 bytes -> 30 bytes measured in isolation.
                *pd.dp = 0;                     // permanent removal (length-1 coin run)
                const u8 reveal = ps.Fetch();   // static metatile to expose
                colp[r] = reveal;               // window now shows the static cell
                if (n < maxOut) out[n] = { c, static_cast<u8>(r), reveal };
                n++;
                break;   // one coin per call -- see the comment above
            }
            if (r != rowBot) { pd.Move(1); ps.Move(1); }   // step down to the next row
        }
        if (n || c == colR) break;
        pd.Move(levelHeight - rows);            // cross to the next column's top row
        ps.Move(levelHeight - rows);
    }
    // n>0: we deliberately stopped after the first coin, so this AABB may still hold
    // another one -- keep re-scanning next frame even if the player doesn't move.
    // n==0 here would mean anyCoin lied (shouldn't happen); treat as verified-empty
    // either way so a stuck AABB can't spin forever re-scanning something that isn't there.
    anchor.dry = (n == 0);
    return n;
}

// Thin wrappers selecting which player's persistent anchor to use -- see
// CollectCoinsImpl's own comment for why the body lives there and not here.
FIXED
u8 CollectCoins(const u16 px, const u16 py, const vec2<u8> dimensions,
                CoinPick* out, const u8 maxOut) {
    return CollectCoinsImpl(colPlayerAnchor, px, py, dimensions, out, maxOut);
}

FIXED
u8 CollectCoins2(const u16 px, const u16 py, const vec2<u8> dimensions,
                 CoinPick* out, const u8 maxOut) {
    return CollectCoinsImpl(colPlayer2Anchor, px, py, dimensions, out, maxOut);
}

}   // namespace level
