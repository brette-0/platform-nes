#include <platform-nes/video.h>
#include <platform-nes/technology.h>
#include "graphics.h"
CHARACTER_ROM_PAD(0x10, 0x00);              // blank tile for 0x00
CHARACTER_ROM(font, "../demo./chr/font");   // font

CHARACTER_ROM_ALIGN(0x2000);                // gen full (SDL3 compat)

CHARMAP(generic,                            // default char map
    CM('h', 0x19)
    CM('i', 0x1a)
);

MAPPED_STRING(generic, msg_hi, hi);