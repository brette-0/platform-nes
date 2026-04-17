#include "handlers.h"
#include <platform-nes/video.h>

#include "levels.h"
#include "platform-nes/audio.h"

extern volatile uint8_t spriteZeroHandled;
extern uint16_t xWorldSpace;

uint8_t TileBuffer[28];
atomic uint8_t levelFetchCommand;

void SpriteZeroHandler(void) {
    spriteZeroHandled = 1;
    SetScroll(xWorldSpace, 16);
    AudioUpdate();
    if (levelFetchCommand & latched) {
        if (levelFetchCommand & next) {
            PopulateFromProvider(TileBuffer, 0, GetNextWrite, 28, 1);
        } else {
            // prev
            PopulateFromProvider(TileBuffer, 0, GetPreviousWrite, 28, 1);
        }
        levelFetchCommand = 0;
    }
}