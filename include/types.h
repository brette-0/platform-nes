#ifndef TYPES_H
#define TYPES_H
typedef struct {
#ifdef TARGET_NES
    const uint8_t *data;          // pointer to famistudio song data
#else
    uint8_t *pcm;                 // raw PCM audio buffer
    uint32_t pcm_len;             // total length in bytes
    uint32_t loop_start;          // byte offset where loop begins (0 = loop whole song)
    uint32_t intro_end;           // byte offset where intro ends (same as loop_start usually)
#endif
} music_t;

typedef struct {
#ifdef TARGET_NES
    uint8_t index;                // SFX index for famistudio
#else
    uint8_t *pcm;
    uint32_t pcm_len;
#endif
} sfx_t;

#endif