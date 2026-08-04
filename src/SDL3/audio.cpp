#include <platform-nes/audio.hpp>
#include <platform-nes/technology.hpp>
#include <SDL3/SDL.h>

// The web build cannot ship the 47MB pc_audio.wav -- Cloudflare Pages enforces
// a 25MB/file limit on the MEMFS blob (demo.data). Instead CMake encodes a ~3MB
// Vorbis pc_audio.ogg for the web bundle, and stb_vorbis decodes it to S16 PCM
// here at load time. Desktop builds are untouched: they keep loading the full
// WAV via SDL_LoadWAV (see LoadTrack below). stb_vorbis.c is header+impl in one
// translation unit; pull it in only on Emscripten so desktop never compiles it.
#ifdef __EMSCRIPTEN__
#define STB_VORBIS_NO_PUSHDATA_API
#define STB_VORBIS_NO_STDIO
#include <stb_vorbis.c>
#endif

typedef struct {
    u32 offset;
    u32 length;
    u32 loop_start;
} track_runtime_t;

static track_runtime_t track_info[256];
float *audio::pcm_buffer = nullptr;
u32 audio::pcm_buffer_size = 0;
static SDL_AudioStream *music_stream = nullptr;
static u32 playback_pos = 0;
static int current_track = -1;
static int music_playing = 0;
static int music_paused = 0;

inline static void PlaySFX(const u8 sample, const u8 index) {

}

static u32 track_offsets[256];
static u32 track_lengths[256];

#define BYTES_PER_SECOND (48000 * 2 * sizeof(float))

// Load one track's source audio into an SDL_LoadWAV-shaped (spec,data,len)
// triple. Desktop reads the WAV directly; the web build swaps the extension to
// .ogg and decodes it with stb_vorbis into S16 PCM (the shared convert path
// downstream resamples whatever spec we hand back). Returns false on failure;
// on success *data is SDL_malloc'd and must be SDL_free'd by the caller, exactly
// like SDL_LoadWAV, so the rest of BuildPCMBuffer is identical on both paths.
static bool LoadTrack(const char *fp, SDL_AudioSpec *spec, u8 **data, u32 *len) {
#ifdef __EMSCRIPTEN__
    char oggpath[260];
    SDL_strlcpy(oggpath, fp, sizeof(oggpath));
    char *dot = SDL_strrchr(oggpath, '.');
    if (dot) SDL_strlcpy(dot, ".ogg", sizeof(oggpath) - (dot - oggpath));

    size_t filelen = 0;
    void *filebuf = SDL_LoadFile(oggpath, &filelen);
    if (!filebuf) return false;

    int channels = 0, rate = 0;
    short *decoded = nullptr;
    const int frames = stb_vorbis_decode_memory(
        static_cast<const unsigned char *>(filebuf),
        static_cast<int>(filelen), &channels, &rate, &decoded);
    SDL_free(filebuf);
    if (frames < 0 || !decoded) return false;

    const u32 bytes = static_cast<u32>(frames) * channels * sizeof(short);
    u8 *out = static_cast<u8 *>(SDL_malloc(bytes));
    SDL_memcpy(out, decoded, bytes);
    free(decoded);  // stb_vorbis allocates with the libc allocator, not SDL's

    spec->format = SDL_AUDIO_S16LE;
    spec->channels = channels;
    spec->freq = rate;
    *data = out;
    *len = bytes;
    return true;
#else
    return SDL_LoadWAV(fp, spec, data, len);
#endif
}

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
        u8 *data;
        u32 len;
        if (LoadTrack(tracks[i].fp, &spec, &data, &len)) {
            u8 *converted = nullptr;
            auto converted_len = 0;
            if (SDL_ConvertAudioSamples(&spec, data, static_cast<int>(len),
                                         &target, &converted, &converted_len)) {
                total += converted_len;
                SDL_free(converted);
            }
            SDL_free(data);
        }
    }

    audio::pcm_buffer = static_cast<float *>(SDL_malloc(total));
    audio::pcm_buffer_size = total;

    // second pass — load, convert, and fill runtime info
    auto offset = 0;
    for (u8 i = 0; i < nTracks; i++) {
        SDL_AudioSpec spec;
        u8 *data;
        u32 len;

        if (!LoadTrack(tracks[i].fp, &spec, &data, &len)) {
            SDL_Log("Failed to load %s: %s", tracks[i].fp, SDL_GetError());
            track_info[i].offset = 0;
            track_info[i].length = 0;
            track_info[i].loop_start = 0;
            continue;
        }

        u8 *converted = nullptr;
        auto converted_len = 0;
        if (SDL_ConvertAudioSamples(&spec, data, static_cast<int>(len),
                                     &target, &converted, &converted_len)) {
            SDL_memcpy(reinterpret_cast<u8 *>(audio::pcm_buffer) + offset, converted, converted_len);

            track_info[i].offset = offset;
            track_info[i].length = converted_len;

            if (tracks[i].loop_start > 0.0f) {
                auto loop_bytes = static_cast<u32>(tracks[i].loop_start * BYTES_PER_SECOND);
                if (loop_bytes > static_cast<u32>(converted_len)) loop_bytes = 0;
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


void audio::SfxPlay(const u8 index, const u8 channel) {
    (void)channel;
    PlaySFX(0, index);
}

void audio::SfxSamplePlay(const u8 index) {
    PlaySFX(1, index);
}

void audio::AudioInit() {
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

void audio::AudioUpdate() {
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
            reinterpret_cast<u8 *>(pcm_buffer) + playback_pos, static_cast<int>(chunk));
        playback_pos += chunk;
        to_feed -= static_cast<int>(chunk);

        if (playback_pos >= end) {
            playback_pos = t->loop_start;
        }
    }
}

void audio::TrackPlay(const u8 index) {
    if (index >= nTracks) return;
    current_track = index;
    playback_pos = track_info[index].offset;
    music_playing = 1;
    music_paused = 0;
    SDL_ClearAudioStream(music_stream);
}

void audio::TrackStop() {
    music_playing = 0;
    current_track = -1;
    SDL_ClearAudioStream(music_stream);
}

void audio::TrackPause(const u8 pause) {
    (void)pause;
    music_paused = !music_paused;
}