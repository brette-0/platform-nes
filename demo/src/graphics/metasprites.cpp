#include <platform-nes/video.hpp>

#include "metasprites.hpp"
#include "graphics.hpp"

#define MS_SPLIT(base) { .tile = (base) }, { .tile = (base) + 1 }, { .tile = (base) + 2 }, { .tile = (base) + 3 }


const oam::sprite_t msMary[0x04] = {
    MS_SPLIT(chrPlayerStanding_tile),  // small mary normal
};
