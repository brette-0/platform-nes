#pragma once

#include <intsh>
using namespace br0::intsh;

// All demo palettes live here.
//
// Background palette: 4 sub-palettes x 4 colours (16 bytes), uploaded to BG_0.
extern const u8 BGColours[16];

// Mary sprite palette: 3 colours following the shared backdrop entry.
extern const u8 maryColors[3];


// Title screen text colours 1-3 (BG palette 3). No colour-0 entry here --
// colour 0 mirrors the shared $3F00 backdrop, which the level's own
// BGColours owns (see title.cpp's DrawLevelPreview) -- the title screen
// itself never decides transparency/backdrop.
extern const u8 titleScreenColours[3];

// Same palette as titleScreenColours, but reachable from LEVEL_GRAPHICS
// (the level's HUD, drawn in palette 3, can't reach the TITLE bank copy).
extern const u8 hudColours[3];