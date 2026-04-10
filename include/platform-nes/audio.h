#ifndef AUDIO_H
#define AUDIO_H

#include <stdint.h>
#include "types.h"


typedef struct {
#ifdef TARGET_NES
    const uint8_t *pTracks;       // pointer to famistudio song data
#else
    const char* fp;
    float    loop_start;    // byte offset where loop begins (0 = loop whole song)
#endif
} music_t;

typedef struct {
#ifdef TARGET_NES
    const uint8_t* pSFX;
    const uint8_t* pSSFX;
#else
    uint8_t *pcm;
    float pcm_len;
#endif
} sfx_t;


#ifndef TARGET_NES
extern float *pcm_buffer;
extern uint32_t pcm_buffer_size;
extern const music_t tracks[];
extern const uint8_t nTracks;

#define TRACKS(...)                         \
    const music_t  tracks[] = {__VA_ARGS__ }; \
    const uint8_t nTracks = sizeof(tracks) / sizeof(music_t);
#else
extern const uint8_t* tracks;
extern const uint8_t* sfx;
#define TRACKS(ptr)                     \
    const uint8_t* tracks = (void*)ptr

#define SFX(ptr)                        \
    const uint8_t* sfx = (void*)ptr;

#endif


void TrackPlay(uint8_t index);
void TrackPause(uint8_t pause);
void TrackStop();
void SfxPlay(uint8_t index, uint8_t channel) ;
void SfxSamplePlay(uint8_t index);
void AudioInit();
void AudioUpdate();

#endif