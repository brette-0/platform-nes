#include "../include/platform-nes/audio.h"
#include <stdint.h>

#ifndef TARGET_NES
TRACKS(
    {.fp ="tracks/pc_audio.wav", .loop_start = 0}
);
#else

extern const uint8_t _music_data_mega_man_2[];
extern const uint8_t _sounds[];

TRACKS(_music_data_mega_man_2);
SFX(_sounds);
#endif