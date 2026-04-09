#ifndef __AUDIO_H
#define __AUDIO_H

#include <stdint.h>

void TrackPlay(uint8_t index);
void TrackPause();
void TrackStop();
void SfxPlay(uint8_t index);
void SfxSamplePlay(uint8_t index);
void AudioInit();
void AudioUpdate();

#endif