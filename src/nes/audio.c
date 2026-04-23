#include "../../include/platform-nes/audio.h"
#include <stdint.h>

#if FAMISTUDIO_CFG_NTSC_SUPPORT
    #define FAMISTUDIO_LOAD_REGION "lda #1\n"
#elif FAMISTUDIO_CFG_PAL_SUPPORT
    #define FAMISTUDIO_LOAD_REGION "lda #0\n"
#else
    #error "FamiStudio: neither NTSC nor PAL support enabled"
#endif

#ifdef __MOS__
    #define FASTCALL __attribute__((cc65_fastcall)) void
#else
    #define FASTCALL void
#endif

#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wattributes"
extern FASTCALL __attribute__((leaf)) famistudio_music_play(uint8_t song_index);
extern FASTCALL __attribute__((leaf)) famistudio_music_pause(uint8_t pause);
extern FASTCALL __attribute__((leaf)) famistudio_music_stop(void);
#if FAMISTUDIO_CFG_SFX_SUPPORT
extern FASTCALL __attribute__((leaf)) famistudio_sfx_play(uint8_t sfx_index, uint8_t channel);
#endif
extern FASTCALL __attribute__((leaf)) famistudio_update(void);
#pragma clang diagnostic pop

void AudioInit(void) {
    __asm__ volatile (
        "ldx #<%0\n"
        "ldy #>%0\n"
        FAMISTUDIO_LOAD_REGION
        "jsr famistudio_init\n"
        :
        : "i"(tracks)
        : "memory", "a", "x", "y", "c", "v"
    );

#if FAMISTUDIO_CFG_SFX_SUPPORT
    __asm__ volatile (
        "ldx #<%0\n"
        "ldy #>%0\n"
        "jsr famistudio_sfx_init\n"
        :
        : "i"(sfx)
        : "memory", "a", "x", "y", "c", "v"
    );
#endif
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
#if FAMISTUDIO_CFG_SFX_SUPPORT
    famistudio_sfx_play(index, channel);
#else
    (void)index; (void)channel;
#endif
}

void SfxSamplePlay(const uint8_t index) {
#if FAMISTUDIO_CFG_SFX_SUPPORT
    famistudio_sfx_play(index, 1);
#else
    (void)index;
#endif
}