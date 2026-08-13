/**
 * @file audio.cpp
 * @brief Audio backend stub for GameCube + Wii.
 *
 * The shared engine expects every backend to define the audio entry points
 * (see audio.hpp) plus the ::pcm_buffer / ::pcm_buffer_size globals the
 * non-NES build references. A full libogc implementation would mix the demo's
 * PCM tracks through ASND/AESND; for the initial homebrew bring-up these are
 * inert so the library links and the demo runs silent. The signatures match
 * the SDL backend exactly, so swapping in a real mixer later touches only this
 * file.
 */
#include <platform-nes/audio.hpp>

/** @brief Shared mixing buffer (unused until a real OGC mixer lands). */
float *audio::pcm_buffer = nullptr;
/** @brief Size of ::pcm_buffer, in samples. */
u32 audio::pcm_buffer_size = 0;

void audio::Init(u8) {}
void audio::Update() {}

void audio::TrackPlay(u8 /*index*/) {}
void audio::TrackPause(u8 /*pause*/) {}
void audio::TrackStop() {}

void audio::SfxPlay(u8 /*index*/, u8 /*channel*/) {}
void audio::SfxSamplePlay(u8 /*index*/) {}
