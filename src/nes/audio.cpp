#include <platform-nes/audio.hpp>
#include <platform-nes/mappers/mmc3.hpp>
#include <intsh>
using namespace br0::intsh;

#if !FAMISTUDIO_CFG_NTSC_SUPPORT && !FAMISTUDIO_CFG_PAL_SUPPORT
    #error "FamiStudio: neither NTSC nor PAL support enabled"
#endif

#ifdef __mos__
    #define FASTCALL __attribute__((cc65_fastcall)) void
#else
    #define FASTCALL void
#endif

/*
 * FamiStudio is assembled by a genuine standalone ca65 binary
 * (CMakeLists.txt), and lives in PRG-ROM banks rather than in the
 * always-resident image: its CODE segment lands in the section
 * ::audio_code_bank_tag names, its exported song data in the one
 * ::audio_data_bank_tag names (see audio.hpp for why those roles are
 * declared by the library but defined by the consuming project).
 *
 * EVERY ENTRY POINT BELOW IS THEREFORE REACHED THROUGH A FARCALL, never a
 * plain call: the engine's own address is only valid while its bank is
 * mapped. mmc3::CallPairedBlock maps BOTH banks at once -- necessary, not
 * belt-and-braces, because the engine walks song data while it executes, so
 * code and data have to be visible simultaneously. That is also why they
 * must sit in different windows; CallPairedBlock static_asserts it.
 *
 * The wrapping lives HERE, in the backend adapter, so callers keep calling
 * audio::Update() and friends with no idea any of it happens -- and so
 * a different engine (C, C++, Rust, hand-written asm) replaces this one
 * file without touching the API, the linker script, or any call site.
 *
 * COST: two bank switches and two restores per call, ~60-70 cycles. Audio
 * update runs once a frame from the main loop, not from the NMI -- if it
 * ever moves into an interrupt, revisit this: the shadow/hardware pair in
 * mmc3::SwitchBank is not currently an atomic critical section.
 */
extern "C" FASTCALL famistudio_music_play(u8 song_index);
extern "C" FASTCALL famistudio_music_pause(u8 pause);
extern "C" FASTCALL famistudio_music_stop(void);
#if FAMISTUDIO_CFG_SFX_SUPPORT
extern "C" FASTCALL famistudio_sfx_play(u8 sfx_index, u8 channel);
#endif
extern "C" FASTCALL famistudio_update(void);

// famistudio_init wants its region byte in A: zero for PAL, non-zero for
// NTSC (famistudio_ca65.s's FAMISTUDIO_INIT doc comment) -- the inverse of
// this project's own Init(region) convention (1 = PAL, 0 = NTSC). `used`
// makes it addressable by symbol from the raw asm text below, the same
// pattern src/nes/input.cpp relies on for its poll scratch bytes (LLVM can't
// see the reference inside opaque asm, so without `used` LTO strips it).
extern "C" {
__attribute__((used)) u8 famistudio_region_scratch;
}

// Every engine entry point goes through this: both banks mapped, call, both
// restored. One name so the pairing can never be spelled inconsistently
// across the file.
template <typename Block>
static decltype(auto) InEngineBanks(Block &&block) {
    return mmc3::CallPairedBlock<audio_code_bank_tag, audio_data_bank_tag>(
        static_cast<Block &&>(block));
}

void audio::Init(const u8 region) {
    famistudio_region_scratch = region ? 0 : 1;

    InEngineBanks([&] {

    __asm__ volatile (
        "ldx #<%0\n"
        "ldy #>%0\n"
        "lda famistudio_region_scratch\n"
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

void audio::TrackPlay(const u8 index) {
    InEngineBanks([&] {
        famistudio_music_play(index);
    });
}

void audio::TrackPause(const u8 pause) {
    InEngineBanks([&] {
        famistudio_music_pause(pause);
    });
}

void audio::TrackStop() {
    InEngineBanks([&] {
        famistudio_music_stop();
    });
}

void audio::Update() {
    InEngineBanks([&] {
        famistudio_update();
    });
}

void audio::SfxPlay(const u8 index, const u8 channel) {
#if FAMISTUDIO_CFG_SFX_SUPPORT
    InEngineBanks([&] { famistudio_sfx_play(index, channel); });
#else
    (void)index; (void)channel;
#endif
}

void audio::SfxSamplePlay(const u8 index) {
#if FAMISTUDIO_CFG_SFX_SUPPORT
    InEngineBanks([&] { famistudio_sfx_play(index, 1); });
#else
    (void)index;
#endif
}
