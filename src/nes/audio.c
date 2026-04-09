#include "../include/audio.h"
#include <stdint.h>

extern const uint8_t region;
extern const uint8_t* pTracks;
extern const uint8_t* pSFX;
extern const uint8_t* pSSFX;
extern const uint8_t* pUseSampleBitfield;

extern void famistudio_init(const uint8_t* data, uint8_t region);
extern void famistudio_play(uint8_t index);
extern void famistudio_music_pause();
extern void famistudio_music_stop();

extern void famistudio_sfx_init(const uint8_t* data);
extern void famistudio_sfx_sample_init(const uint8_t* samples);
extern void famistudio_sfx_sample_play(uint8_t index);
extern void famistudio_sfx_play(uint8_t index);

extern void famistudio_update();

inline void AudioInit() {
    famistudio_init(pTracks, region);
    famistudio_sfx_init(pSFX);
    famistudio_sfx_sample_init(pSSFX);
}

inline void TrackPlay(uint8_t index) {
    famistudio_play(index);
}

inline void TrackPause() {
    famistudio_music_pause();
}

inline void TrackStop() {
    famistudio_music_stop();
}

inline void AudioUpdate() {
    famistudio_update();
}

inline void SFXPlay(uint8_t index) {
    famistudio_sfx_sample_play(index);
}

inline void SfxSamplePlay(uint8_t index) {
    famistudio_sfx_play(index);
}