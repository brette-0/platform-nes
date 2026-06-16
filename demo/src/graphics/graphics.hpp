#pragma once

// Single source of truth for the demo's CHR tiles.
//
// Each blob is pulled in with #embed, so its size -- and therefore its base
// CHR-tile index `<name>_tile` -- is a compile-time constant usable in
// constexpr tables (see metatiles.cpp / MT_SPLIT). Tile ids are never
// hand-assigned: each chains from the blob before it.
//
//   * Just #include this wherever you use tile ids. The FINAL blob's
//     CHARACTER_ROM_END_FINAL emits the padded 8 KB CHR image as an `inline`
//     object, so the linker folds it to exactly one copy no matter how many
//     TUs include this header -- no dedicated emit TU and no magic define.
//     One includer is cheapest; extra includers are simply safe.
//
// sprite-0 graphics MUST stay first (tile 0) for sprite-0 hit detection.
// #embed paths are relative to THIS header (demo/src/graphics/).

#include <platform-nes/video.hpp>

CHARACTER_ROM_BEGIN(chrSprite0)
#embed "../../chr/sprites/sprite0.chr"
CHARACTER_ROM_END(chrSprite0, CHR_ORIGIN);

// player sprite graphics
CHARACTER_ROM_BEGIN(chrPlayerStanding)
#embed "../../chr/sprites/player/standing.chr"
CHARACTER_ROM_END(chrPlayerStanding, chrSprite0);

// power sprite graphics
CHARACTER_ROM_BEGIN(chrBerries)
#embed "../../chr/sprites/berries.chr"
CHARACTER_ROM_END(chrBerries, chrPlayerStanding);

CHARACTER_ROM_BEGIN(chrWand)
#embed "../../chr/sprites/wand.chr"
CHARACTER_ROM_END(chrWand, chrBerries);

// enemy sprite graphics
CHARACTER_ROM_BEGIN(chrMushletStanding)
#embed "../../chr/sprites/enemies/mushlet/standing.chr"
CHARACTER_ROM_END(chrMushletStanding, chrWand);

// world static tiles
CHARACTER_ROM_BEGIN(chrBush)
#embed "../../chr/tiles/static/bush.chr"
CHARACTER_ROM_END(chrBush, chrMushletStanding);

CHARACTER_ROM_BEGIN(chrLiquid)
#embed "../../chr/tiles/static/liquid.chr"
CHARACTER_ROM_END(chrLiquid, chrBush);

CHARACTER_ROM_BEGIN(chrPipe)
#embed "../../chr/tiles/static/pipe.chr"
CHARACTER_ROM_END(chrPipe, chrLiquid);

CHARACTER_ROM_BEGIN(chrTerrain)
#embed "../../chr/tiles/static/terrain.chr"
CHARACTER_ROM_END(chrTerrain, chrPipe);

CHARACTER_ROM_BEGIN(chrAir)
#embed "../../chr/tiles/static/terrain.chr"
CHARACTER_ROM_END(chrAir, chrTerrain);

// ui static tiles
CHARACTER_ROM_BEGIN(chrFont)
#embed "../../chr/tiles/static/ui/font.chr"
CHARACTER_ROM_END(chrFont, chrAir);

CHARACTER_ROM_BEGIN(chrHUDWhitespace)
#embed "../../chr/tiles/static/ui/hud_whitespace.chr"
CHARACTER_ROM_END(chrHUDWhitespace, chrFont);

// world dynamic tiles -- FINAL blob. Closing it with _FINAL also emits the
// whole cartridge's CHR ROM image: 0x2000 (8 KB) is the entire CHR ROM here,
// which (being a single page) the PPU maps directly. A banked cartridge would
// pass its full CHR ROM size instead and let the mapper window it in.
CHARACTER_ROM_BEGIN(chrCoin)
#embed "../../chr/tiles/dynamic/coin.chr"
CHARACTER_ROM_END_FINAL(chrCoin, chrHUDWhitespace, 0x2000);
