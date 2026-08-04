#include <platform-nes/audio.hpp>
#include <platform-nes/mappers/vrc1.hpp>
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
 * BANKED CALL, Phase 6: FamiStudio is a real, existing opaque-vendor-
 * library case -- source available, but assembled by a genuine standalone
 * ca65 binary (CMakeLists.txt), not this project's own C++ codegen, and
 * living in its own dedicated bank (prg_rom_famistudio, demo/link.ld) once
 * real multi-bank content exists, unlike the "everything fits in one bank"
 * assumption these calls used to quietly depend on (see
 * BANKED_CALL_THEORY.txt's "WORKED EXAMPLE: FAMISTUDIO"). Since this
 * project's own CMake drives ca65's invocation, BANKED_EXTERN can tag
 * FamiStudio's own entry points directly -- no wrapper function needed,
 * unlike the genuinely opaque/no-source-no-build-control case
 * BANKED_CALL_THEORY.txt's LIMITATIONS section separately resolves.
 *
 * FAMISTUDIO_SEGMENT arrives unquoted (CMakeLists.txt's
 * target_compile_definitions, matching every other FAMISTUDIO_CFG_* knob)
 * specifically to avoid shell-escaping a pre-quoted string through a raw
 * command-line -D -- stringized here instead, the standard two-level macro
 * idiom (a single #FAMISTUDIO_SEGMENT would stringize the macro NAME, not
 * its expanded VALUE, if FAMISTUDIO_SEGMENT itself were ever further
 * macro-defined -- it isn't today, but this is the same load-bearing
 * indirection BANKED_IMPL's own comment describes for exactly this reason).
 */
#define FAMISTUDIO_SEGMENT_STR_HELPER(x) #x
#define FAMISTUDIO_SEGMENT_STR(x) FAMISTUDIO_SEGMENT_STR_HELPER(x)

struct famistudio_tag {};

extern "C" const u8 __famistudio_domain_size[];

template <> struct bank_layout<famistudio_tag> {
    static constexpr bool always_mapped = false;
    static section_t section() {
        // Base bank (6) IS hand-entered and constexpr-safe -- prg_rom_famistudio
        // is its own dedicated region (demo/link.ld), same reasoning as
        // bank3_test_tag's. Only size is a genuine runtime read: FamiStudio's
        // real compiled footprint isn't something anyone should have to
        // hand-declare, and CallInSection transparently upgrades to the
        // two-window path if it ever measures larger than one window.
        return { (static_cast<u32>(6) << 16) | 0xc000, reinterpret_cast<u32>(__famistudio_domain_size) };
    }
};

// BANKED_EXTERN's own extern "C" covers linkage here -- no surrounding
// extern "C" block, since the bank_of<> specialization it also emits is a
// template and templates must have C++ (not C) linkage.
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wattributes"
BANKED_EXTERN(FAMISTUDIO_SEGMENT_STR(FAMISTUDIO_SEGMENT), famistudio, FASTCALL, famistudio_music_play, u8 song_index);
BANKED_EXTERN(FAMISTUDIO_SEGMENT_STR(FAMISTUDIO_SEGMENT), famistudio, FASTCALL, famistudio_music_pause, u8 pause);
BANKED_EXTERN(FAMISTUDIO_SEGMENT_STR(FAMISTUDIO_SEGMENT), famistudio, FASTCALL, famistudio_music_stop, void);
#if FAMISTUDIO_CFG_SFX_SUPPORT
BANKED_EXTERN(FAMISTUDIO_SEGMENT_STR(FAMISTUDIO_SEGMENT), famistudio, FASTCALL, famistudio_sfx_play, u8 sfx_index, u8 channel);
#endif
BANKED_EXTERN(FAMISTUDIO_SEGMENT_STR(FAMISTUDIO_SEGMENT), famistudio, FASTCALL, famistudio_update, void);
#pragma clang diagnostic pop

void AudioInit() {
    // famistudio_init/famistudio_sfx_init are called from raw asm (no C++
    // function pointer exists to BANKED_EXTERN directly), so famistudio_update
    // -- already registered above -- is used purely as the anchor to resolve
    // famistudio_tag's window; every entry point in this domain shares it.
    CallBlock<famistudio_update>([&] {
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
    });
}

void TrackPlay(const u8 index) {
    Call<famistudio_music_play>(index);
}

void TrackPause(const u8 pause) {
    Call<famistudio_music_pause>(pause);
}

void TrackStop() {
    Call<famistudio_music_stop>();
}

void AudioUpdate() {
    Call<famistudio_update>();
}

void SfxPlay(const u8 index, const u8 channel) {
#if FAMISTUDIO_CFG_SFX_SUPPORT
    Call<famistudio_sfx_play>(index, channel);
#else
    (void)index; (void)channel;
#endif
}

void SfxSamplePlay(const u8 index) {
#if FAMISTUDIO_CFG_SFX_SUPPORT
    Call<famistudio_sfx_play>(index, 1);
#else
    (void)index;
#endif
}