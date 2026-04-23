/**
 * @file audio.h
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

#include <stdint.h>
#include "types.h"


/**
 * @brief A single music track.
 *
 * On the NES, a track is a pointer to FamiStudio song data baked into
 * ROM. On desktop builds, a track is a filesystem path plus a loop
 * point expressed as a byte offset (0 means "loop the whole song").
 */
typedef struct {
#ifdef TARGET_NES
    const uint8_t *pTracks;       /**< Pointer to FamiStudio song data in ROM. */
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
    const uint8_t* pSFX;          /**< Pointer to the FamiStudio SFX bank. */
    const uint8_t* pSSFX;         /**< Pointer to the FamiStudio sample-SFX bank. */
#else
    uint8_t *pcm;                 /**< Decoded PCM sample data. */
    float pcm_len;                /**< Length of ::pcm in samples. */
#endif
} sfx_t;


#ifndef TARGET_NES
/** @brief Shared mixing buffer written by the SDL3 audio callback. */
extern float *pcm_buffer;
/** @brief Size of ::pcm_buffer, in samples. */
extern uint32_t pcm_buffer_size;
/** @brief Application-defined track table; defined via ::TRACKS. */
extern const music_t tracks[];
/** @brief Number of entries in ::tracks. */
extern const uint8_t nTracks;

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
    const uint8_t nTracks = sizeof(tracks) / sizeof(music_t);
#else
/** @brief Pointer to the FamiStudio music bank (NES build). */
extern const uint8_t* tracks;
/** @brief Pointer to the FamiStudio SFX bank (NES build). */
extern const uint8_t* sfx;
/**
 * @brief Binds a FamiStudio music bank for the NES build.
 * @param ptr Symbol or address of the FamiStudio music export.
 */
#define TRACKS(ptr)                     \
    const uint8_t* tracks = (void*)ptr

/**
 * @brief Binds a FamiStudio SFX bank for the NES build.
 * @param ptr Symbol or address of the FamiStudio SFX export.
 */
#define SFX(ptr)                        \
    const uint8_t* sfx = (void*)ptr;

#endif


/**
 * @brief Starts playback of the track at the given table index.
 * @param index Index into the application-declared track table.
 */
void TrackPlay(uint8_t index);

/**
 * @brief Pauses or resumes the currently playing track.
 * @param pause Non-zero to pause; zero to resume.
 */
void TrackPause(uint8_t pause);

/** @brief Stops the currently playing track and releases its resources. */
void TrackStop();

/**
 * @brief Plays a sound effect on a specific mixer channel.
 * @param index   SFX bank index.
 * @param channel Target audio channel.
 */
void SfxPlay(uint8_t index, uint8_t channel) ;

/**
 * @brief Plays a sample-based sound effect (DPCM on NES).
 * @param index SFX sample index.
 */
void SfxSamplePlay(uint8_t index);

/** @brief Initialises the audio backend; must be called before any other audio function. */
void AudioInit();

/**
 * @brief Drives the audio engine forward by one frame.
 *
 * Must be called once per video frame to advance the FamiStudio player
 * (NES) or to refill the PCM mix buffer (desktop).
 */
void AudioUpdate();

#endif
