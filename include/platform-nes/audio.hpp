/**
 * @file audio.hpp
 * @brief Music and sound-effect playback API.
 *
 * Provides a uniform interface over two very different audio backends:
 *
 * - **NES**: drives the FamiStudio sound engine directly; tracks and
 *   SFX are raw byte streams in ROM.
 * - **SDL3**: decodes PCM from disk and mixes it into a shared output
 *   buffer.
 *
 * Application code should use the ::TRACKS and ::SFX macros to register
 * assets at file scope, then call the play/update helpers at runtime.
 */
#pragma once
#include <intsh>
using namespace br0::intsh;


#ifdef TARGET_NES
#include <platform-nes/mappers/mmc3.hpp>

/**
 * @brief Bank tags for a PRG-ROM-banked audio backend -- DECLARED here,
 *        DEFINED by the consuming project.
 *
 * The audio backend's code and its music data may each live in their own
 * PRG-ROM bank. Which banks those are is a layout decision, so this library
 * only names the two roles; a consuming project supplies the facts by
 * specialising mmc3::bank_layout for each (see demo/src/banks.hpp for a
 * worked example, and mmc3::CallPairedBlock for what is done with them).
 *
 * A project that does NOT bank its audio still has to specialise both -- with
 * `always_mapped = true`, one line each. That makes every call below compile
 * to a plain call with no bank switching at all, so nothing is paid for a
 * feature that isn't used. Leaving them unspecialised is a compile error
 * ("implicit instantiation of undefined template mmc3::bank_layout<...>")
 * rather than a silent wrong-bank call at runtime.
 *
 * DELIBERATELY NOT NAMED AFTER FAMISTUDIO. FamiStudio is this project's
 * current backend, not a permanent one: a replacement engine written in C,
 * C++, Rust or hand-written assembly binds to these same two roles by
 * placing itself in the same sections, with no change here or in any
 * consuming project's linker script.
 */
struct audio_code_bank_tag {}; ///< The audio engine's own code.
struct audio_data_bank_tag {}; ///< Music/SFX data the engine reads while running.
#endif

namespace audio {

/**
 * @brief A single music track.
 *
 * On the NES, a track is a pointer to FamiStudio song data baked into
 * ROM. On desktop builds, a track is a filesystem path plus a loop
 * point expressed as a byte offset (0 means "loop the whole song").
 */
typedef struct {
#ifdef TARGET_NES
    const u8 *pTracks;       /**< Pointer to FamiStudio song data in ROM. */
#else
    const char* fp;               /**< Filesystem path to the PCM/ogg/wav source. */
    float    loop_start;          /**< Byte offset where the loop begins (0 = loop the whole song). */
#endif
} music_t;

/**
 * @brief A single sound effect.
 *
 * NES effects reference two FamiStudio sound banks (regular and
 * sample). Desktop/console-port effects are a filesystem path to a
 * one-shot PCM/ogg/wav source, mirroring ::music_t's desktop shape;
 * each backend decodes it once at init into its own internal mixing
 * buffer, the same way it does for ::tracks.
 */
typedef struct {
#ifdef TARGET_NES
    const u8* pSFX;          /**< Pointer to the FamiStudio SFX bank. */
    const u8* pSSFX;         /**< Pointer to the FamiStudio sample-SFX bank. */
#else
    const char* fp;          /**< Filesystem path to the PCM/ogg/wav source. */
#endif
} sfx_t;

} // namespace audio

#ifndef TARGET_NES
namespace audio {
/** @brief Shared mixing buffer written by the SDL3 audio callback. */
extern float *pcm_buffer;
/** @brief Size of ::pcm_buffer, in samples. */
extern u32 pcm_buffer_size;
}
/** @brief Application-defined track table; defined via ::TRACKS. */
extern const audio::music_t tracks[];
/** @brief Number of entries in ::tracks. */
extern const u8 nTracks;

/**
 * @brief Declares the application's track table (desktop build).
 *
 * Expands to definitions of ::tracks and ::nTracks. Use once at file
 * scope with a brace-enclosed list of ::audio::music_t initialisers.
 *
 * @code
 *   TRACKS(
 *     { "assets/title.ogg", 0.0f },
 *     { "assets/stage1.ogg", 12.5f },
 *   )
 * @endcode
 */
#define TRACKS(...)                         \
    const audio::music_t  tracks[] = {__VA_ARGS__ }; \
    const u8 nTracks = sizeof(tracks) / sizeof(audio::music_t);

/** @brief Application-defined SFX table; defined via ::SFX. Each backend
 *         provides a weak zero-entry default, so apps that declare no SFX
 *         still link. */
extern const audio::sfx_t sfxs[];
/** @brief Number of entries in ::sfxs. */
extern const u8 nSfx;

/**
 * @brief Declares the application's one-shot SFX table (desktop/console-port build).
 *
 * Expands to definitions of ::sfxs and ::nSfx. Use once at file scope with
 * a brace-enclosed list of ::audio::sfx_t initialisers.
 *
 * @code
 *   SFX(
 *     { "assets/jump.wav" },
 *     { "assets/coin.wav" },
 *   )
 * @endcode
 */
#define SFX(...)                            \
    const audio::sfx_t sfxs[] = {__VA_ARGS__ }; \
    const u8 nSfx = sizeof(sfxs) / sizeof(audio::sfx_t);
#else
/** @brief Pointer to the FamiStudio music bank (NES build). */
extern const u8* tracks;
/** @brief Pointer to the FamiStudio SFX bank (NES build). */
extern const u8* sfx;
/**
 * @brief Binds a FamiStudio music bank for the NES build.
 * @param ptr Symbol or address of the FamiStudio music export.
 */
#define TRACKS(ptr)                     \
    const u8* tracks = (const u8*)(ptr)

/**
 * @brief Binds a FamiStudio SFX bank for the NES build.
 * @param ptr Symbol or address of the FamiStudio SFX export.
 */
#define SFX(ptr)                        \
    const u8* sfx = (const u8*)(ptr);

#endif

namespace audio {

/**
 * @brief Starts playback of the track at the given table index.
 * @param index Index into the application-declared track table.
 */
void TrackPlay(u8 index);

/**
 * @brief Pauses or resumes the currently playing track.
 * @param pause Non-zero to pause; zero to resume.
 */
void TrackPause(u8 pause);

/** @brief Stops the currently playing track and releases its resources. */
void TrackStop();

/**
 * @brief Plays a sound effect on a specific mixer channel.
 *
 * A channel is a fixed voice slot: starting a new effect on a channel that
 * is still playing cuts the old one off, matching FamiStudio's own SFX
 * engine on the NES. Channels are otherwise independent and mix together.
 *
 * @param index   SFX bank index.
 * @param channel Target audio channel.
 */
void SfxPlay(u8 index, u8 channel) ;

/**
 * @brief Plays a sample-based sound effect (DPCM on NES).
 * @param index SFX sample index.
 */
void SfxSamplePlay(u8 index);

/**
 * @brief Initializes the audio backend; must be called before any other audio function.
 * @param region Playback region: 0 for NTSC (default), 1 for PAL.
 */
void Init(u8 region = 0);

/**
 * @brief Drives the audio engine forward by one frame.
 *
 * Must be called once per video frame to advance the FamiStudio player
 * (NES) or to refill the PCM mix buffer (desktop).
 */
void Update();

} // namespace audio