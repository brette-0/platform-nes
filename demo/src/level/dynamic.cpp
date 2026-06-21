#include "dynamic.hpp"

#include "levels.hpp"
#include "../graphics/metatiles.hpp"

#include <array>
#include <intsh>

using namespace br0::intsh;

namespace demo::level {
    // ---------------------------------------------------------------------------
    // Dynamic-plane definitions.  See dynamic.hpp for the model.  This TU is fully
    // additive: it touches no existing engine state, only its own RAM/ROM handles.
    // ---------------------------------------------------------------------------

    // RAM-resident run-data (BSS, ~128 bytes).  Zero-initialized => all air until a
    // level loads a dynamic layer via LoadDynamicLayer.
    u8 DynData[DynRunCapacity];

    // Null until a level ships a dynamic plane.  CollidesDynamic / DynamicCursor are
    // only ever reached through that path, so the null state is never walked.
    const u8* DynLengths = nullptr;
    const u8* DynDataROM = nullptr;

    // Edge walkers, kept in lockstep with level::edgeR / edgeL (see dynamic.hpp).
    // Seeded to the level start alongside their static partners in RESET.
    DynamicCursor dynEdgeR;
    DynamicCursor dynEdgeL;

    // Walk the dynamic RLE by `amt` metatiles.  Byte-for-byte the same engine as
    // Cursor::Move (including the run-0 floor guard that stops offset under-flowing
    // to 0xFFFF), but lengths come from DynLengths (this plane's ROM run-lengths)
    // rather than the global HunkLengths.  Duplicated rather than shared for the
    // prototype; fold the two behind one walker once the dynamic path is proven.
    void DynamicCursor::Move(i16 amt) {
        // Hoist to locals (see Cursor::Move): mutating offset/progress through `this` each step
        // is a ~200-cycle memory round-trip; held in locals they live in zero-page registers and
        // write back once.  This walker is on the player re-anchor's hot path every frame.
        u8  p = progress;
        // Running pointer into DynLengths (see Cursor::Move): avoids the clc/adc/adc reconstruction
        // of DynLengths[o] on every metatile stepped; lp advances with the run index, o is recovered
        // from it once at the end.
        const u8* lp = DynLengths + offset;
        // unroll(disable): mirror of Cursor::Move -- constant-step callers (Move(levelHeight)
        // column crossings in ColMapTrack/Seed) were unrolled 14x per inlined site, the dynamic
        // half of the same PRG blow-up.  Keep it rolled; the per-step cost is the RLE compare.
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
                if (lp == DynLengths) break;   // floor at the level start (see Cursor::Move)
                --lp;
                p = *lp;
            }
            --p;
            ++amt;
        }
        offset = static_cast<u16>(lp - DynLengths);
        progress = p;
    }

    // AABB sweep over the dynamic plane.  Same column-major cell walk as
    // CollidesSolid, but returns the FIRST overlapped non-air dynamic tile (id +
    // owning run) instead of a bool, so the caller can dispatch a removal rule.
    DynHit CollidesDynamic(const DynamicCursor& origin, const u16 px, const u16 py, const u8 w, const u8 h) {
        const u16 rowTop = py >> 4;
        if (rowTop >= levelHeight) return {false, 0, 0};

        u16 rowBot = (py + h - 1) >> 4;
        if (rowBot >= levelHeight) rowBot = levelHeight - 1;

        const u8 cols = ((px + w - 1) >> 4) - (px >> 4);
        const u8 rows = static_cast<u8>(rowBot - rowTop);

        // ONE cursor, walked monotonically forward through the AABB cells in
        // column-major order -- NEVER reset to origin.  Within a column each row is a
        // single Move(1); crossing to the next column steps the remaining
        // (levelHeight - rows) cells.  Total RLE stepping is the last cell's offset
        // (~levelHeight for a 2-wide box), not the SUM of every cell's offset: the old
        // per-cell `probe = origin; Move(c*H + r)` re-walked the whole first column
        // from scratch for every second-column cell -- O(cols*levelHeight) of pure
        // redundant 16-bit-indexed stepping, the exact RLE walk the static plane was
        // moved off of.
        DynamicCursor probe = origin;
        for (u8 c = 0; ; c++) {
            for (u8 r = 0; r <= rows; r++) {
                if (const u8 id = probe.Fetch(); id != 0)
                    return {true, id, probe.Run()};
                if (r != rows) probe.Move(1);                // next row, same column
            }
            if (c == cols) break;
            probe.Move(static_cast<i16>(levelHeight - rows)); // cross to next column, row 0
        }
        return {false, 0, 0};
    }

    // ---------------------------------------------------------------------------
    // Removal jump table.  Each dynamic metatile id maps to a rule run when the tile
    // is consumed.  File-local (internal linkage) so the constexpr table folds at
    // build time with no global symbol; RemoveDynamic below is the public, non-inline
    // trampoline into it.  Mirrors the Actor plain-function-pointer dispatch style.
    // ---------------------------------------------------------------------------
    namespace {

        using DynRule = void (*)(const DynHit&, Cursor);

        // Default: tile has no removal behavior (shouldn't be reached for air, which
        // CollidesDynamic filters, but keeps the table total).
        void RuleNop(const DynHit&, Cursor) {}

        // Blank the run in RAM (so the tile stops colliding / rendering) and reveal what
        // the static plane holds underneath -- avoiding the SMB1 "coin on a wall becomes
        // air" artifact.  `under` is a static cursor already parked on the same cell.
        void RuleReveal(const DynHit& self, const Cursor &under) {
            DynData[self.run] = 0;            // consume: this run is now air
            const u8 beneath = under.Fetch(); // static metatile to expose
            (void)beneath;                    // STUB: queue a nametable patch of `beneath`
        }

        // Coin: reveal-underneath, plus the game-feel response.
        void RuleCoin(const DynHit& self, Cursor under) {
            RuleReveal(self, under);
            // STUB: score++, coin SFX, HUD refresh.
        }

        constexpr u8 DYN_COIN = 0x62;   // coin metatile id (see metatiles.cpp)

        constexpr std::array<DynRule, 256> BuildDynRules() {
            std::array<DynRule, 256> t{};
            for (auto& e : t) e = &RuleNop;
            t[DYN_COIN] = &RuleCoin;
            return t;
        }

        constexpr std::array<DynRule, 256> DynRemovalRules = BuildDynRules();

    }   // namespace

    // Public dispatch trampoline: look the hit's id up in the jump table and run its
    // rule.  Non-inline so the internal-linkage table never leaks into a header.
    void RemoveDynamic(const DynHit& h, const Cursor &under) {
        DynRemovalRules[h.id](h, under);
    }

    // ROM->RAM load for a level's dynamic plane.  STUB: uncalled until a level ships
    // a dynamic layer -- LoadLevel will call this with the level's dynamic lengths /
    // data (or nullptr / 0 for a level that has none).
    void LoadDynamicLayer(const u8* dynLengthsROM, const u8* dynDataROM, u8 runCount) {
        DynLengths = dynLengthsROM;
        DynDataROM = dynDataROM;
        for (u8 i = 0; i < runCount; ++i)
            DynData[i] = dynDataROM ? dynDataROM[i] : 0;
    }

}   // namespace demo::level
