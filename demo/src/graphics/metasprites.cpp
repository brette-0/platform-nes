#include <platform-nes/video.hpp>

#include "metasprites.hpp"
#include "graphics.hpp"

#define MS_SPLIT(base) { .tile = (base) }, { .tile = (base) + 1 }, { .tile = (base) + 2 }, { .tile = (base) + 3 }


const oam::sprite_t msMary[0x04] = {
    MS_SPLIT(chrPlayerStanding_tile),  // small mary normal
};

const oam::sprite_t msMary2[0x04] = {
    { .tile = chrPlayerStanding_tile     , .attributes = 0x40 },
    { .tile = chrPlayerStanding_tile + 1 , .attributes = 0x40 },
    { .tile = chrPlayerStanding_tile + 2 , .attributes = 0x40 },
    { .tile = chrPlayerStanding_tile + 3 , .attributes = 0x40 },
};
