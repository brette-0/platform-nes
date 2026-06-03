#include <platform-nes/audio.hpp>
#include <intsh>
using namespace br0::intsh;

#if FAMISTUDIO_CFG_NTSC_SUPPORT
    #define FAMISTUDIO_LOAD_REGION "lda #1\n"
#elif FAMISTUDIO_CFG_PAL_SUPPORT
    #define FAMISTUDIO_LOAD_REGION "lda #0\n"
#else
    #error "FamiStudio: neither NTSC nor PAL support enabled"
#endif

#ifdef __mos__
    #define FASTCALL __attribute__((cc65_fastcall)) void
#else
    #define FASTCALL void
#endif

#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wattributes"
#ifdef __cplusplus
extern "C" {
#endif
extern FASTCALL __attribute__((leaf)) famistudio_music_play(u8 song_index);
extern FASTCALL __attribute__((leaf)) famistudio_music_pause(u8 pause);
extern FASTCALL __attribute__((leaf)) famistudio_music_stop(void);
#if FAMISTUDIO_CFG_SFX_SUPPORT
extern FASTCALL __attribute__((leaf)) famistudio_sfx_play(u8 sfx_index, u8 channel);
#endif
extern FASTCALL __attribute__((leaf)) famistudio_update(void);
#ifdef __cplusplus
}
#endif
#pragma clang diagnostic pop

void AudioInit() {
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

void TrackPlay(const u8 index) {
    famistudio_music_play(index);
}

void TrackPause(const u8 pause) {
    famistudio_music_pause(pause);
}

void TrackStop() {
    famistudio_music_stop();
}

void AudioUpdate() {
    famistudio_update();
}

void SfxPlay(const u8 index, const u8 channel) {
#if FAMISTUDIO_CFG_SFX_SUPPORT
    famistudio_sfx_play(index, channel);
#else
    (void)index; (void)channel;
#endif
}

void SfxSamplePlay(const u8 index) {
#if FAMISTUDIO_CFG_SFX_SUPPORT
    famistudio_sfx_play(index, 1);
#else
    (void)index;
#endif
}