#include "handlers.h"
#include <platform-nes/video.h>

#include "levels.h"
#include "platform-nes/audio.h"

extern volatile uint8_t spriteZeroHandled;
extern uint16_t xWorldSpace;


atomic uint8_t levelStreamCommand;
uint8_t TileBuffer[56];

void SpriteZeroHandler(void) {
    spriteZeroHandled = 1;
    SetScroll(xWorldSpace, 16);
    AudioUpdate();
    if (levelStreamCommand & STREAM_LEVEL_LATCH) {
        if (levelStreamCommand & STREAM_LEVEL_RIGHT) {
            PopulateFromProvider(TileBuffer, 0,  GetNextWrite, 28, 1);
            PopulateFromProvider(TileBuffer, 28, GetCurrentWrite, 28, 1);
        } else {
            // left
        }
        levelStreamCommand = STREAM_LEVEL_DONE;
    }
}