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
#ifndef AUDIO_H
#define AUDIO_H

#include <intsh>
using namespace br0::intsh;


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
 * sample); desktop effects wrap a raw PCM buffer plus its length.
 */
typedef struct {
#ifdef TARGET_NES
    const u8* pSFX;          /**< Pointer to the FamiStudio SFX bank. */
    const u8* pSSFX;         /**< Pointer to the FamiStudio sample-SFX bank. */
#else
    u8 *pcm;                 /**< Decoded PCM sample data. */
    float pcm_len;                /**< Length of ::pcm in samples. */
#endif
} sfx_t;


#ifndef TARGET_NES
/** @brief Shared mixing buffer written by the SDL3 audio callback. */
extern float *pcm_buffer;
/** @brief Size of ::pcm_buffer, in samples. */
extern u32 pcm_buffer_size;
/** @brief Application-defined track table; defined via ::TRACKS. */
extern const music_t tracks[];
/** @brief Number of entries in ::tracks. */
extern const u8 nTracks;

/**
 * @brief Declares the application's track table (desktop build).
 *
 * Expands to definitions of ::tracks and ::nTracks. Use once at file
 * scope with a brace-enclosed list of ::music_t initialisers.
 *
 * @code
 *   TRACKS(
 *     { "assets/title.ogg", 0.0f },
 *     { "assets/stage1.ogg", 12.5f },
 *   )
 * @endcode
 */
#define TRACKS(...)                         \
    const music_t  tracks[] = {__VA_ARGS__ }; \
    const u8 nTracks = sizeof(tracks) / sizeof(music_t);
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
 * @param index   SFX bank index.
 * @param channel Target audio channel.
 */
void SfxPlay(u8 index, u8 channel) ;

/**
 * @brief Plays a sample-based sound effect (DPCM on NES).
 * @param index SFX sample index.
 */
void SfxSamplePlay(u8 index);

/** @brief Initializes the audio backend; must be called before any other audio function. */
void AudioInit();

/**
 * @brief Drives the audio engine forward by one frame.
 *
 * Must be called once per video frame to advance the FamiStudio player
 * (NES) or to refill the PCM mix buffer (desktop).
 */
void AudioUpdate();

#endif
