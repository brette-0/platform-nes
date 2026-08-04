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

/*
 * FamiStudio is assembled by a genuine standalone ca65 binary
 * (CMakeLists.txt), not this project's own C++ codegen, but is otherwise
 * ordinary, always-resident content in this demo's flat PRG-ROM image (see
 * demo/link.ld: FSCODE lands in prg_rom_resident, the same region as
 * everything else) -- no farcall/bank-switching machinery needed to reach
 * it. The VRC1 version of this file (and BANKED_CALL_THEORY.txt's "WORKED
 * EXAMPLE: FAMISTUDIO") gave it its own dedicated bank purely to exercise
 * that project's BANKED_CALL system; this demo has no frequent cross-bank
 * calling and no interrupt-driven bank switching, so plain extern "C"
 * declarations and ordinary calls are all that's needed here.
 */
extern "C" FASTCALL famistudio_music_play(u8 song_index);
extern "C" FASTCALL famistudio_music_pause(u8 pause);
extern "C" FASTCALL famistudio_music_stop(void);
#if FAMISTUDIO_CFG_SFX_SUPPORT
extern "C" FASTCALL famistudio_sfx_play(u8 sfx_index, u8 channel);
#endif
extern "C" FASTCALL famistudio_update(void);

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
