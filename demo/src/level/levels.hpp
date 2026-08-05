#pragma once

#include "types.hpp"
#include "cursor.hpp"
#include "dynamic.hpp"   // dynEdgeR / dynEdgeL, lockstep with edgeR / edgeL

#include <intsh>
#include <platform-nes/technology.hpp>
#include <platform-nes/types.hpp>      // vec2
using namespace br0::intsh;

namespace demo::level {
    constexpr auto levelHeight = 14;
    constexpr i16 kHudRows = 1;
    extern u16 nColumns;
    extern const u8* TileData;
    extern const u8 LevelDataAttributes[];
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
    void BuildNextColumn(u8* buf, u16 worldCol);   // right-scroll: reads window column
    void BuildPrevColumn(u8* buf, u16 worldCol);   // left-scroll: reads window column
    bool LoadLevel(u16 n);
}
