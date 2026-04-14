#include <platform-nes/video.h>
#include <platform-nes/technology.h>
#include "graphics.h"

CHARACTER_ROM(crate, "../demo./chr/all");

CHARACTER_ROM_ALIGN(0x2000);                // gen full (SDL3 compat)

CHARMAP(generic,                            // default char map
    CM('h', 0x11)
    CM('i', 0x12)
);

MAPPED_STRING(generic, msg_hi, hi);

const uint8_t BGColours[] = {0x31, 0x11, 0x21, 0x0e};