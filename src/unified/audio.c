#include "../include/audio.h"

#include <SDL3/SDL.h>
#include "internal.h"
#include "types.h"

typedef struct {
    uint32_t offset;
    uint32_t length;
    uint32_t loop_start;
} track_runtime_t;

static track_runtime_t track_info[256];
float *pcm_buffer = NULL;
uint32_t pcm_buffer_size = 0;
static SDL_AudioStream *music_stream = NULL;
static uint32_t playback_pos = 0;
static int current_track = -1;
static int music_playing = 0;
static int music_paused = 0;

inline static void PlaySFX(const uint8_t sample, const uint8_t index) {

}

static uint32_t track_offsets[256];
static uint32_t track_lengths[256];

#define BYTES_PER_SECOND (48000 * 2 * sizeof(float))

static void BuildPCMBuffer(void) {
    // first pass — measure total converted size
    const SDL_AudioSpec target = {
        .format = SDL_AUDIO_F32LE,
        .channels = 2,
        .freq = 48000
    };

    uint32_t total = 0;
    for (uint8_t i = 0; i < nTracks; i++) {
        SDL_AudioSpec spec;
        uint8_t *data;
        uint32_t len;
        if (SDL_LoadWAV(tracks[i].fp, &spec, &data, &len)) {
            uint8_t *converted = NULL;
            int converted_len = 0;
            if (SDL_ConvertAudioSamples(&spec, data, (int)len,
                                         &target, &converted, &converted_len)) {
                total += converted_len;
                SDL_free(converted);
            }
            SDL_free(data);
        }
    }

    pcm_buffer = SDL_malloc(total);
    pcm_buffer_size = total;

    // second pass — load, convert, and fill runtime info
    uint32_t offset = 0;
    for (uint8_t i = 0; i < nTracks; i++) {
        SDL_AudioSpec spec;
        uint8_t *data;
        uint32_t len;

        if (!SDL_LoadWAV(tracks[i].fp, &spec, &data, &len)) {
            SDL_Log("Failed to load %s: %s", tracks[i].fp, SDL_GetError());
            track_info[i].offset = 0;
            track_info[i].length = 0;
            track_info[i].loop_start = 0;
            continue;
        }

        uint8_t *converted = NULL;
        int converted_len = 0;
        if (SDL_ConvertAudioSamples(&spec, data, (int)len,
                                     &target, &converted, &converted_len)) {
            SDL_memcpy((uint8_t *)pcm_buffer + offset, converted, converted_len);

            track_info[i].offset = offset;
            track_info[i].length = converted_len;

            if (tracks[i].loop_start > 0.0f) {
                uint32_t loop_bytes = (uint32_t)(tracks[i].loop_start * BYTES_PER_SECOND);
                if (loop_bytes > (uint32_t)converted_len) loop_bytes = 0;
                track_info[i].loop_start = offset + loop_bytes;
            } else {
                track_info[i].loop_start = offset;
            }

            offset += converted_len;
            SDL_free(converted);
        }

        SDL_free(data);
    }
}


void SfxPlay(const uint8_t index, const uint8_t channel) {
    PlaySFX(0, index);
}

void SfxSamplePlay(uint8_t index) {
    PlaySFX(1, index);
}

void AudioInit(void) {
    SDL_AudioSpec spec = {
        .format = SDL_AUDIO_F32LE,
        .channels = 2,
        .freq = 48000
    };
    music_stream = SDL_OpenAudioDeviceStream(
        SDL_AUDIO_DEVICE_DEFAULT_PLAYBACK, &spec, NULL, NULL);
    if (!music_stream) {
        SDL_Log("AUDIO: stream creation failed: %s", SDL_GetError());
        return;
    }
    SDL_Log("AUDIO: stream created OK");
    SDL_ResumeAudioStreamDevice(music_stream);

    BuildPCMBuffer();
    SDL_Log("AUDIO: buffer built, %u bytes, %d tracks", pcm_buffer_size, nTracks);

    for (int i = 0; i < nTracks; i++) {
        SDL_Log("AUDIO: track %d: offset=%u length=%u loop=%u",
            i, track_info[i].offset, track_info[i].length, track_info[i].loop_start);
    }
}

void AudioUpdate(void) {
    if (!music_playing || music_paused || current_track < 0) return;

    int queued = SDL_GetAudioStreamQueued(music_stream);
    if (queued > 32768) return;  // keep ~85ms buffered

    track_runtime_t *t = &track_info[current_track];
    uint32_t end = t->offset + t->length;

    // feed enough for several frames
    int to_feed = 32768 - queued;
    while (to_feed > 0) {
        uint32_t remaining = end - playback_pos;
        uint32_t chunk = remaining < 16384 ? remaining : 16384;

        SDL_PutAudioStreamData(music_stream,
            (uint8_t *)pcm_buffer + playback_pos, chunk);
        playback_pos += chunk;
        to_feed -= chunk;

        if (playback_pos >= end) {
            playback_pos = t->loop_start;
        }
    }
}

void TrackPlay(uint8_t index) {
    if (index >= nTracks) return;
    current_track = index;
    playback_pos = track_info[index].offset;
    music_playing = 1;
    music_paused = 0;
    SDL_ClearAudioStream(music_stream);
}

void TrackStop(void) {
    music_playing = 0;
    current_track = -1;
    SDL_ClearAudioStream(music_stream);
}

void TrackPause(const uint8_t pause) {
    music_paused = !music_paused;
}