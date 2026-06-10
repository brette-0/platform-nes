#include "metasprites.hpp"
#include <platform-nes/video.hpp>

const oam::sprite_t msMary[0x10] = {
    { .tile = 0xfe }, { .tile = 0xfe }, { .tile = 0xfe }, { .tile = 0xfe },  // small mary top
    { .tile = 0x00 }, { .tile = 0x01 }, { .tile = 0x10 }, { .tile = 0x11 },  // small mary bottom
    { .tile = 0x00 }, { .tile = 0x01 }, { .tile = 0x4c }, { .tile = 0x4d },  // large mary top
    { .tile = 0x4a }, { .tile = 0x4a }, { .tile = 0x4b }, { .tile = 0x4b },  // large mary bottom
};
