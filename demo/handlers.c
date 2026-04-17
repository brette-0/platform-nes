#include "handlers.h"
#include <platform-nes/video.h>
#include <stdint.h>
#include "levels.h"
#include "main.h"
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
            if (levelStreamCommand & STREAM_LEVEL_SWAP) {
                hunk_remaining = LevelDataLengths[level_data_index] - hunk_remaining;
                for (uint16_t i = 0; i < VIEWPORT_MX * LEVEL_HEIGHT; i++) {
                    GetNextMetaTile();
                }
            }
            PopulateFromProvider(TileBuffer, 0,  GetNextWrite, 28, 1);
            PopulateFromProvider(TileBuffer, 28, GetCurrentWrite, 28, 1);
        } else {
            if (levelStreamCommand & STREAM_LEVEL_SWAP) {
                hunk_remaining = LevelDataLengths[level_data_index] - hunk_remaining;
                for (uint16_t i = 0; i < VIEWPORT_MX * LEVEL_HEIGHT; i++) {
                    GetPrevMetaTile();
                }
            }
            PopulateFromProvider(TileBuffer, 28,  GetPrevWrite, 28, 1);
            PopulateFromProvider(TileBuffer, 0, GetCurrentWrite, 28, 1);
        }
        levelStreamCommand &= ~STREAM_LEVEL_LATCH;
        levelStreamCommand |=  STREAM_LEVEL_DONE;
    }
}