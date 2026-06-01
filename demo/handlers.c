#include "handlers.h"

#include <stdbool.h>
#include <platform-nes/video.h>
#include <stdint.h>

#include "levels.h"
#include "main.h"
#include "platform-nes/audio.h"


atomic uint8_t levelStreamCommand;
uint8_t TileBuffer[56];

#define PLAYER_SCROLL_POS (VIEWPORT_PX >> 1)

void SpriteZeroHandler(void) {
    spriteZeroHandled = 1;
    SetScroll(xWorldSpace, 16);

    AudioUpdate();

    const int8_t deltaX = !!(port1 & LEFT) * -1 + !!(port1 & RIGHT) * 1; // NOLINT(*-narrowing-conversions)
    const int8_t deltaY = !!(port1 & UP  ) * -1 + !!(port1 & DOWN ) * 1; // NOLINT(*-narrowing-conversions)

    if (deltaY) {
        playerY += deltaY;
        PopulateFromProvider(
            (uint8_t*)&oamBuffer,
            SPRITE_SLOT(1) + offsetof(struct sprite_t, y),
            AdjustSpriteY, 8, SPRITE_STRIDE
        );
    }

    if (!deltaX) return;    // if no need to move player sprite x or sroll, return early

    const bool couldScroll = playerX == PLAYER_SCROLL_POS && (
                                (deltaX > 0 && xWorldSpace + deltaX <= (uint16_t)((levelSize - VIEWPORT_MX) << 4)) |
                                ((uint16_t)(xWorldSpace + deltaX) < xWorldSpace)
    );

    if (couldScroll) {
        if (deltaX > 0) xWorldSpace++;
        else            xWorldSpace--;

        if (!(xWorldSpace & 0x0f)) {
            if (deltaX > 0) {
                if (xWorldSpace > lastXWorldSpace && xWorldSpace != (levelSize - VIEWPORT_MX) << 4) {
                    levelStreamCommand = STREAM_LEVEL_RIGHT;
                    if (lastDeltaScroll < 0) {
                        levelStreamCommand |= STREAM_LEVEL_SWAP;
                        hunk_remaining = LevelDataLengths[level_data_index] - hunk_remaining;
                        for (uint16_t i = 0; i < VIEWPORT_MX * LEVEL_HEIGHT + 2 * LEVEL_HEIGHT; i++) {
                            GetNextMetaTile();
                        }
                    }


                    PopulateFromProvider(TileBuffer, 0,  GetNextWrite, 28, 1);
                    PopulateFromProvider(TileBuffer, 28, GetCurrentNext, 28, 1);
                    lastDeltaScroll = deltaX;
                    lastXWorldSpace = xWorldSpace;
                    levelStreamCommand |=  STREAM_LEVEL_DONE;
                }
            } else if (xWorldSpace == lastXWorldSpace - 0x10) {
                levelStreamCommand = STREAM_LEVEL_LEFT;
                if (lastDeltaScroll > 0) {
                    levelStreamCommand |= STREAM_LEVEL_SWAP;
                    hunk_remaining = LevelDataLengths[level_data_index] - hunk_remaining;
                    for (uint16_t i = 0; i < VIEWPORT_MX * LEVEL_HEIGHT + 2 * LEVEL_HEIGHT; i++) {
                        GetPrevMetaTile();
                    }
                }

                lastDeltaScroll = deltaX;
                lastXWorldSpace = xWorldSpace;
                PopulateFromProvider(TileBuffer, 55,  GetPrevWrite, 28, -1);
                PopulateFromProvider(TileBuffer, 27, GetCurrentPrev, 28, -1);
                levelStreamCommand |=  STREAM_LEVEL_DONE;
            }
        }
    } else {
        const video_t lastPlayerX = playerX;
        playerX += deltaX;
        if (playerX > (video_t)-16 && deltaX > 0) {
            playerX = -16;
        } else if (playerX > lastPlayerX && deltaX < 0) {
            playerX = 0;
        }

        PopulateFromProvider(
            (uint8_t*)&oamBuffer,
            SPRITE_SLOT(1) + offsetof(struct sprite_t, x),
            AdjustSpriteX, 8, SPRITE_STRIDE
        );
    }
}