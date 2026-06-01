#include "handlers.h"

#include <stdbool.h>
#include <platform-nes/video.h>
#include <stdint.h>

#include "levels.h"
#include "main.h"
#include "platform-nes/audio.h"

atomic uint8_t levelStreamCommand;
uint8_t TileBuffer[56];

#define MARIO_MAX_COARSE_X (VIEWPORT_PX - 16)
#define MARIO_MAX_COARSE_Y (256         - 16)
#define MARIO_SCROLL_POS   ((VIEWPORT_PX - 16) >> 1)

// per-frame tasks that don't do anything PPU volatile on the current frame
void SpriteZeroHandler(void) {
    spriteZeroHandled = 1;
    SetScroll(xWorldSpace, 16);

    AudioUpdate();

    // process player inputs on player [rudimentary]
    const video_t lastMarioX = marioXPosCoarse;

    const int8_t movDeltaX = !!(port1 & LEFT) * -1 + !!(port1 & RIGHT) * 1; // NOLINT(*-narrowing-conversions)
    const int8_t movDeltaY = !!(port1 & UP  ) * -1 + !!(port1 & DOWN ) * 1; // NOLINT(*-narrowing-conversions)

    // check if y was changed
    if (movDeltaY) {
        marioYPosCoarse += movDeltaY;
        PopulateFromProvider(
            (uint8_t*)&oamBuffer,
            SPRITE_SLOT(1) + offsetof(struct sprite_t, y),
            AdjustSpriteY, 8, SPRITE_STRIDE
        );
    }

    if (!movDeltaX) return; // if there is no x movement, we don't need to move mario's sprite x pos or scroll

    // consult if we need to scroll
    if (marioXPosCoarse == MARIO_SCROLL_POS && (movDeltaX > 0    // check if scroll is impossible (we should move player)
        ? xWorldSpace + 1 < (levelSize - VIEWPORT_MX) << 4
        : xWorldSpace - 1 < xWorldSpace)) {
        // choosing to scroll (verified scroll is possible and the player is in position)

        xWorldSpace += movDeltaX;

        if (!levelStreamCommand && !(xWorldSpace & 0x0f)) {
            if (movDeltaX > 0) {
                if (xWorldSpace != (levelSize - VIEWPORT_MX) << 4) {
                    levelStreamCommand = STREAM_LEVEL_RIGHT;
                    if (lastDeltaScroll < 0) {
                        // swap
                        levelStreamCommand |= STREAM_LEVEL_SWAP;
                        hunk_remaining = LevelDataLengths[level_data_index] - hunk_remaining;
                        for (uint16_t i = 0; i < (uint16_t)(VIEWPORT_MX * LEVEL_HEIGHT + 2 * LEVEL_HEIGHT); i++) {
                            GetNextMetaTile();
                        }
                    }

                    PopulateFromProvider(TileBuffer, 0,  GetNextWrite, 28, 1);
                    PopulateFromProvider(TileBuffer, 28, GetCurrentNext, 28, 1);
                    lastDeltaScroll = movDeltaX;
                    lastXWorldSpace = xWorldSpace;

                }
            } else if (xWorldSpace == lastXWorldSpace - 0x10) {
                levelStreamCommand = STREAM_LEVEL_LEFT;
                if (lastDeltaScroll > 0) {
                    // swap
                    levelStreamCommand |= STREAM_LEVEL_SWAP;
                    hunk_remaining = LevelDataLengths[level_data_index] - hunk_remaining;
                    for (uint16_t i = 0; i < (uint16_t)(VIEWPORT_MX * LEVEL_HEIGHT + 2 * LEVEL_HEIGHT); i++) {
                        GetPrevMetaTile();
                    }
                }

                PopulateFromProvider(TileBuffer, 55,  GetPrevWrite, 28, -1);
                PopulateFromProvider(TileBuffer, 27, GetCurrentPrev, 28, -1);
                lastDeltaScroll = movDeltaX;
                lastXWorldSpace = xWorldSpace;
            }
        }

        levelStreamCommand |=  STREAM_LEVEL_DONE;
        return; // do not move sprite if we chose to scroll instead
    }

    // otherwise, we move sprite
    marioXPosCoarse += movDeltaX;

    if (movDeltaX > 0 && marioXPosCoarse > MARIO_MAX_COARSE_X) {
        marioXPosCoarse = -16;
    } else if (movDeltaX < 0 && marioXPosCoarse > lastMarioX) {
        marioXPosCoarse = 0;
    }

    // only update x if actually needed
    PopulateFromProvider(
        (uint8_t*)&oamBuffer,
        SPRITE_SLOT(1) + offsetof(struct sprite_t, x),
        AdjustSpriteX, 8, SPRITE_STRIDE
    );
}