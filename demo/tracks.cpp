#include "../include/platform-nes/audio.hpp"
#include <cstdint>

#ifndef TARGET_NES
TRACKS(
    {.fp ="tracks/pc_audio.wav", .loop_start = 0}
);
#else

#ifdef __cplusplus
extern "C" {
#endif
extern const std::uint8_t _music_data_mega_man_2[];
extern const std::uint8_t _sounds[];
#ifdef __cplusplus
}
#endif

TRACKS(_music_data_mega_man_2);
SFX(_sounds);
#endif