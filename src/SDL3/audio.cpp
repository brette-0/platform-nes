#include <platform-nes/audio.hpp>
#include <platform-nes/technology.hpp>
#include <SDL3/SDL.h>

typedef struct {
    std::uint32_t offset;
    std::uint32_t length;
    std::uint32_t loop_start;
} track_runtime_t;

static track_runtime_t track_info[256];
float *pcm_buffer = nullptr;
std::uint32_t pcm_buffer_size = 0;
static SDL_AudioStream *music_stream = nullptr;
static std::uint32_t playback_pos = 0;
static int current_track = -1;
static int music_playing = 0;
static int music_paused = 0;

inline static void PlaySFX(const std::uint8_t sample, const std::uint8_t index) {

}

static std::uint32_t track_offsets[256];
static std::uint32_t track_lengths[256];

#define BYTES_PER_SECOND (48000 * 2 * sizeof(float))

static void BuildPCMBuffer() {
    // first pass — measure total converted size
    constexpr SDL_AudioSpec target = {
        .format = SDL_AUDIO_F32LE,
        .channels = 2,
        .freq = 48000
    };

    auto total = 0;
    for (auto i = 0; i < nTracks; i++) {
        SDL_AudioSpec spec;
        std::uint8_t *data;
        std::uint32_t len;
        if (SDL_LoadWAV(tracks[i].fp, &spec, &data, &len)) {
            std::uint8_t *converted = nullptr;
            auto converted_len = 0;
            if (SDL_ConvertAudioSamples(&spec, data, static_cast<int>(len),
                                         &target, &converted, &converted_len)) {
                total += converted_len;
                SDL_free(converted);
            }
            SDL_free(data);
        }
    }

    pcm_buffer = static_cast<float *>(SDL_malloc(total));
    pcm_buffer_size = total;

    // second pass — load, convert, and fill runtime info
    auto offset = 0;
    for (std::uint8_t i = 0; i < nTracks; i++) {
        SDL_AudioSpec spec;
        std::uint8_t *data;
        std::uint32_t len;

        if (!SDL_LoadWAV(tracks[i].fp, &spec, &data, &len)) {
            SDL_Log("Failed to load %s: %s", tracks[i].fp, SDL_GetError());
            track_info[i].offset = 0;
            track_info[i].length = 0;
            track_info[i].loop_start = 0;
            continue;
        }

        std::uint8_t *converted = nullptr;
        auto converted_len = 0;
        if (SDL_ConvertAudioSamples(&spec, data, static_cast<int>(len),
                                     &target, &converted, &converted_len)) {
            SDL_memcpy(reinterpret_cast<std::uint8_t *>(pcm_buffer) + offset, converted, converted_len);

            track_info[i].offset = offset;
            track_info[i].length = converted_len;

            if (tracks[i].loop_start > 0.0f) {
                auto loop_bytes = static_cast<std::uint32_t>(tracks[i].loop_start * BYTES_PER_SECOND);
                if (loop_bytes > static_cast<std::uint32_t>(converted_len)) loop_bytes = 0;
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


void SfxPlay(const std::uint8_t index, const std::uint8_t channel) {
    (void)channel;
    PlaySFX(0, index);
}

void SfxSamplePlay(const std::uint8_t index) {
    PlaySFX(1, index);
}

void AudioInit() {
    constexpr SDL_AudioSpec spec = {
        .format = SDL_AUDIO_F32LE,
        .channels = 2,
        .freq = 48000
    };
    music_stream = SDL_OpenAudioDeviceStream(
        SDL_AUDIO_DEVICE_DEFAULT_PLAYBACK, &spec, nullptr, nullptr);
    if (!music_stream) {
        SDL_Log("AUDIO: stream creation failed: %s", SDL_GetError());
        return;
    }
    SDL_ResumeAudioStreamDevice(music_stream);

    BuildPCMBuffer();
}

void AudioUpdate() {
    if (!music_playing || music_paused || current_track < 0) return;

    const auto queued = SDL_GetAudioStreamQueued(music_stream);
    if (queued > 32768) return;  // keep ~85ms buffered

    const track_runtime_t *t = &track_info[current_track];
    const auto end = t->offset + t->length;

    // feed enough for several frames
    auto to_feed = 32768 - queued;
    while (to_feed > 0) {
        const auto remaining = end - playback_pos;
        const auto chunk = remaining < 16384 ? remaining : 16384;

        SDL_PutAudioStreamData(music_stream,
            reinterpret_cast<std::uint8_t *>(pcm_buffer) + playback_pos, static_cast<int>(chunk));
        playback_pos += chunk;
        to_feed -= static_cast<int>(chunk);

        if (playback_pos >= end) {
            playback_pos = t->loop_start;
        }
    }
}

void TrackPlay(const std::uint8_t index) {
    if (index >= nTracks) return;
    current_track = index;
    playback_pos = track_info[index].offset;
    music_playing = 1;
    music_paused = 0;
    SDL_ClearAudioStream(music_stream);
}

void TrackStop() {
    music_playing = 0;
    current_track = -1;
    SDL_ClearAudioStream(music_stream);
}

void TrackPause(const std::uint8_t pause) {
    (void)pause;
    music_paused = !music_paused;
}