#include "../../include/platform-nes/audio.h"
#include <stdint.h>

#if defined(__clangd__) || defined(__JETBRAINS_IDE__)
    #define FASTCALL void
#else
    // 2. SATISFY THE HARDWARE: The real compiler gets the hyphenated attribute
    // If your compiler version panics on '-', change it to cc65_fastcall
    #define FASTCALL __attribute__((cc65_fastcall)) void
#endif

// FamiStudio C bindings — these are the actual exported symbol names
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wattributes"
extern FASTCALL __attribute__((leaf)) famistudio_init(uint8_t lo, uint8_t hi);
extern FASTCALL __attribute__((leaf)) famistudio_music_play(uint8_t song_index);
extern FASTCALL __attribute__((leaf)) famistudio_music_pause(uint8_t pause);
extern FASTCALL __attribute__((leaf)) famistudio_music_stop(void);
extern FASTCALL __attribute__((leaf)) famistudio_sfx_init(void *sfx_data);
extern FASTCALL __attribute__((leaf)) famistudio_sfx_play(uint8_t sfx_index, uint8_t channel);
extern FASTCALL __attribute__((leaf)) famistudio_update(void);
#pragma clang diagnostic pop




void AudioInit(void) {
    __asm__ volatile (
        // Init music
        "ldx #<%0\n"
        "ldy #>%0\n"
        "lda #1\n"
        "jsr famistudio_init\n"

        // Init SFX
        "ldx #<%1\n"
        "ldy #>%1\n"
        "jsr famistudio_sfx_init\n"
        :
        : "i"(tracks), "i"(sfx)
        : "memory"
    );
}

void TrackPlay(const uint8_t index) {
    __asm__ volatile (
        "lda #<%0\n"
        "jsr famistudio_music_play\n"
        :
        : "i"(index)
        : "memory"
    );
}

void TrackPause(const uint8_t pause) {
    __asm__ volatile (
        "lda #<%0\n"
        "jsr famistudio_music_pause\n"
        :
        : "i"(pause)
        : "memory"
    );
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