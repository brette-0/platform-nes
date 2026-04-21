#include "../../include/platform-nes/audio.h"
#include <stdint.h>

#if defined(__clangd__) || defined(__JETBRAINS_IDE__)
    #define FASTCALL void
#else
    // 2. SATISFY THE HARDWARE: The real compiler gets the hyphenated attribute
    // If your compiler version panics on '-', change it to cc65_fastcall
    #define FASTCALL __attribute__((cc65_fastcall)) void
#endif

// FamiStudio C bindings — these are the actual exported symbol names.
// The single-byte-arg routines use the A register, which matches cc65_fastcall's
// last-argument-in-A rule, so they can be called directly from C.
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wattributes"
extern FASTCALL __attribute__((leaf)) famistudio_music_play(uint8_t song_index);
extern FASTCALL __attribute__((leaf)) famistudio_music_pause(uint8_t pause);
extern FASTCALL __attribute__((leaf)) famistudio_music_stop(void);
extern FASTCALL __attribute__((leaf)) famistudio_sfx_play(uint8_t sfx_index, uint8_t channel);
extern FASTCALL __attribute__((leaf)) famistudio_update(void);
#pragma clang diagnostic pop

// famistudio_init / famistudio_sfx_init take their pointer argument split
// across X (lo) and Y (hi) — a convention cc65_fastcall cannot express.
// Isolate the register marshaling in noinline wrappers so every caller sees
// a plain C call, which llvm-mos can reason about for bank placement.
__attribute__((noinline))
static void famistudio_init_c(const void *music_data, uint8_t region) {
    const uint8_t lo = (uint8_t)((uintptr_t)music_data & 0xFF);
    const uint8_t hi = (uint8_t)((uintptr_t)music_data >> 8);
    __asm__ volatile (
        "jsr famistudio_init\n"
        :
        : "x"(lo), "y"(hi), "a"(region)
        : "memory"
    );
}

__attribute__((noinline))
static void famistudio_sfx_init_c(const void *sfx_data) {
    const uint8_t lo = (uint8_t)((uintptr_t)sfx_data & 0xFF);
    const uint8_t hi = (uint8_t)((uintptr_t)sfx_data >> 8);
    __asm__ volatile (
        "jsr famistudio_sfx_init\n"
        :
        : "x"(lo), "y"(hi)
        : "memory"
    );
}


void AudioInit(void) {
    famistudio_init_c(tracks, 1);
    famistudio_sfx_init_c(sfx);
}

void TrackPlay(const uint8_t index) {
    famistudio_music_play(index);
}

void TrackPause(const uint8_t pause) {
    famistudio_music_pause(pause);
}

void TrackStop(void) {
    famistudio_music_stop();
}

void AudioUpdate(void) {
    famistudio_update();
}

void SfxPlay(const uint8_t index, const uint8_t channel) {
    famistudio_sfx_play(index, channel);
}

void SfxSamplePlay(const uint8_t index) {
    famistudio_sfx_play(index, 1);
}