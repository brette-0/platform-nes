#include "handlers.h"

#include <platform-nes/video.h>
#include <stdint.h>

#include "levels.h"
#include "main.h"
#include "platform-nes/audio.h"


atomic uint8_t levelStreamCommand;
uint8_t TileBuffer[56];

#define PLAYER_SCROLL_POS   (VIEWPORT_PX >> 1)
#define PLAYER_MAX_SPRITE_X (VIEWPORT_PX - 16)

void SpriteZeroHandler(void) {
    SetScroll(xWorldSpace, 16);
    PollControllers(&port1, &port2);
    spriteZeroHandled = 1;

    AudioUpdate();

    const int8_t deltaX = !!(port1 & LEFT) * -1 + !!(port1 & RIGHT) * 1; // NOLINT(*-narrowing-conversions)
    const int8_t deltaY = !!(port1 & UP  ) * -1 + !!(port1 & DOWN ) * 1; // NOLINT(*-narrowing-conversions)

    if (deltaY) {
        playerY += deltaY;
        PopulateOAMFromProvider(OAMBuffer, 1, y, AdjustSpriteY, 8);
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
            } else if (xWorldSpace == lastXWorldSpace - 16) {
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
        const oam_t lastPlayerX = playerX;
        playerX += deltaX;
        if (deltaX > 0) {
            if (playerX > PLAYER_MAX_SPRITE_X) {
                playerX = PLAYER_MAX_SPRITE_X;
            }
        } else {
            if (playerX > lastPlayerX) {
                playerX = 0;
            }
        }

        PopulateOAMFromProvider(OAMBuffer, 1, x, AdjustSpriteX, 8);
    }
}