#include "metasprites.h"
#include <platform-nes/video.h>
#include <stdint.h>

const struct sprite_t msMario[0x10] = {
    { .tile = 0xfc }, { .tile = 0xfc }, { .tile = 0xfc }, { .tile = 0xfc },  // small mario top
    { .tile = 0x3a }, { .tile = 0x37 }, { .tile = 0x4f }, { .tile = 0x4f },  // small mario bottom
    { .tile = 0x00 }, { .tile = 0x01 }, { .tile = 0x4c }, { .tile = 0x4d },  // large mario top
    { .tile = 0x4a }, { .tile = 0x4a }, { .tile = 0x4b }, { .tile = 0x4b },  // large mario bottom
};
