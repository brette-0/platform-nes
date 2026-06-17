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

// RAM-resident run-data (BSS, ~128 bytes).  Zero-initialised => all air until a
// level loads a dynamic layer via LoadDynamicLayer.
u8 DynData[DynRunCapacity];

// Null until a level ships a dynamic plane.  CollidesDynamic / DynamicCursor are
// only ever reached through that path, so the null state is never walked.
const u8* DynLengths = nullptr;
const u8* DynDataROM = nullptr;

// Walk the dynamic RLE by `amt` metatiles.  Byte-for-byte the same engine as
// Cursor::Move (including the run-0 floor guard that stops offset underflowing
// to 0xFFFF), but lengths come from DynLengths (this plane's ROM run-lengths)
// rather than the global HunkLengths.  Duplicated rather than shared for the
// prototype; fold the two behind one walker once the dynamic path is proven.
void DynamicCursor::Move(i16 amt) {
    while (amt > 0) {
        if (++progress >= DynLengths[offset]) {
            offset++;
            progress = 0;
        }
        --amt;
    }
    while (amt < 0) {
        if (progress == 0) {
            if (offset == 0) return;   // floor at the level start (see Cursor::Move)
            offset--;
            progress = DynLengths[offset];
        }
        --progress;
        ++amt;
    }
}

// AABB sweep over the dynamic plane.  Same column-major cell walk as
// CollidesSolid, but returns the FIRST overlapped non-air dynamic tile (id +
// owning run) instead of a bool, so the caller can dispatch a removal rule.
DynHit CollidesDynamic(const DynamicCursor& origin, u16 px, u16 py, u8 w, u8 h) {
    const u16 rowTop = py >> 4;
    if (rowTop >= levelHeight) return {false, 0, 0};

    u16 rowBot = (py + h - 1) >> 4;
    if (rowBot >= levelHeight) rowBot = levelHeight - 1;

    const u8 cols = ((px + w - 1) >> 4) - (px >> 4);
    const u8 rows = static_cast<u8>(rowBot - rowTop);

    for (u8 c = 0; c <= cols; c++) {
        for (u8 r = 0; r <= rows; r++) {
            DynamicCursor probe = origin;
            probe.Move(c * levelHeight + r);   // column-major: dCol*H + dRow
            const u8 id = probe.Fetch();
            if (id != 0)
                return {true, id, probe.Run()};
        }
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

// Default: tile has no removal behaviour (shouldn't be reached for air, which
// CollidesDynamic filters, but keeps the table total).
void RuleNop(const DynHit&, Cursor) {}

// Blank the run in RAM (so the tile stops colliding / rendering) and reveal what
// the static plane holds underneath -- avoiding the SMB1 "coin on a wall becomes
// air" artefact.  `under` is a static cursor already parked on the same cell.
void RuleReveal(const DynHit& self, Cursor under) {
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
void RemoveDynamic(const DynHit& h, Cursor under) {
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

// ---------------------------------------------------------------------------
// Integration sketches -- compiled but uncalled.  They document exactly where
// the two remaining wire-ups land once real dynamic level data and actor
// collision exist, without yet touching the live actor / render paths.
// ---------------------------------------------------------------------------

// Point (2): the actor double-probe.  After the static CollidesSolid test, an
// actor also probes the dynamic plane; a hit dispatches the tile's removal rule,
// passing the static cursor so the rule can read what sits underneath.
[[maybe_unused]] static void ActorDynamicProbeExample(
    const Cursor& staticCursor, const DynamicCursor& dynCursor,
    u16 px, u16 py, u8 w, u8 h) {
    const DynHit dyn = CollidesDynamic(dynCursor, px, py, w, h);
    if (dyn.hit)
        RemoveDynamic(dyn, staticCursor);
}

// Point (3): render compositing.  The dynamic plane draws OVER the static one,
// so a column builder takes the dynamic metatile when non-air, else the static.
[[maybe_unused]] static u8 CompositeCell(u8 staticMetatile, u8 dynamicMetatile) {
    return dynamicMetatile != 0 ? dynamicMetatile : staticMetatile;
}

}   // namespace demo::level
