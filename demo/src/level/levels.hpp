#pragma once

#include "types.hpp"
#include "cursor.hpp"

#include <intsh>
#include <platform-nes/technology.hpp>
using namespace br0::intsh;

namespace demo::level {
    constexpr auto levelHeight = 14;
    extern u16 nColumns;
    extern const u8* TileData;
    extern const u8* HunkLengths;
    extern const u8 LevelDataAttributes[];
    // Two independent RLE walkers, one parked at each camera edge.  edgeR feeds
    // the right-edge column stream (forward), edgeL the left-edge column stream
    // (backward).  Cursor positions are direction-agnostic, so reversing scroll
    // just switches which edge feeds -- no re-walk / mirror of the run counter.
    extern Cursor edgeR;
    extern Cursor edgeL;   // seeded valid at reset, kept in lockstep with edgeR
    extern u8 attr_column;
    extern u8 AttributeBuffer[8];
    extern const Level Levels[];
    u8 GetPrevWrite(u8 step);
    u8 GetNextWrite(u8 step);
    u8 GetCurrentNext(u8 step);
    u8 GetCurrentPrev(u8 step);
    u8 GetPrevMetaTile();
    u8 GetNextMetaTile();
    // Same-TU column builders: the provider loops live next to the providers so
    // the optimiser folds them in (no per-tile call through a function pointer).
    void BuildNextColumn(u8* buf);   // right-scroll: forward edge
    void BuildPrevColumn(u8* buf);   // left-scroll: backward edge
    MINSIZE bool BuildLevelSize();
    bool LoadLevel(u16 n);
}
