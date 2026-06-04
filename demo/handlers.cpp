#include "levels.hpp"
#include "main.hpp"
#include "handlers.hpp"

#include <platform-nes/audio.hpp>
#include <platform-nes/video.hpp>
#include <intsh>
using namespace br0::intsh;

atomic u8 levelStreamCommand;
u8 TileBuffer[56];

void SpriteZeroHandler() {
    // Recomputed per call: on NES these fold to literals (-O3); on desktop the
    // viewport is a runtime value, so they must be read after video init, not
    // bound to a constexpr / static-init global.
    // ReSharper disable once CppVariableCanBeMadeConstexpr
    const auto playerScrollPos = (video::viewport_px() >> 1);
    // ReSharper disable once CppVariableCanBeMadeConstexpr
    const auto playerMaxXPos   = (video::viewport_px() - 16);

    ppu::SetScroll(xWorldSpace, 16);
    PollControllers(&port1, &port2);
    spriteZeroHandled = 1;

    AudioUpdate();

    const i8 deltaX = !!(port1 & LEFT) * -1 + !!(port1 & RIGHT) * 1; // NOLINT(*-narrowing-conversions)
    // ReSharper disable once CppTooWideScope
    const i8 deltaY = !!(port1 & UP  ) * -1 + !!(port1 & DOWN ) * 1; // NOLINT(*-narrowing-conversions)

    if (deltaY) {
        playerY += deltaY;
        oam::PopulateFromProvider(OAMBuffer, 1, oam::y, AdjustSpriteY, 8);
    }

    if (!deltaX) return;    // if no need to move player sprite x or sroll, return early

    const bool couldScroll = playerX == playerScrollPos
        && ((deltaX > 0 && xWorldSpace + deltaX <= (levelSize - viewport_mx()) << 4)
            | (xWorldSpace + deltaX < xWorldSpace));

    if (couldScroll) {
        if (deltaX > 0) xWorldSpace = xWorldSpace + 1;
        else            xWorldSpace = xWorldSpace - 1;

        if (!(xWorldSpace & 0x0f)) {
            if (deltaX > 0) {
                if (xWorldSpace > lastXWorldSpace && xWorldSpace != (levelSize - viewport_mx()) << 4) {
                    levelStreamCommand = STREAM_LEVEL_RIGHT;
                    if (lastDeltaScroll < 0) {
                        levelStreamCommand = levelStreamCommand | STREAM_LEVEL_SWAP;
                        hunk_remaining = LevelDataLengths[level_data_index] - hunk_remaining;
                        for (auto i = 0; i < viewport_mx() * levelHeight + 2 * levelHeight; i++) {
                            GetNextMetaTile();
                        }
                    }


                    PopulateFromProvider(TileBuffer, 0,  GetNextWrite, 28, 1);
                    PopulateFromProvider(TileBuffer, 28, GetCurrentNext, 28, 1);
                    lastDeltaScroll = deltaX;
                    lastXWorldSpace = xWorldSpace;
                    levelStreamCommand = levelStreamCommand | STREAM_LEVEL_DONE;
                }
            } else if (xWorldSpace == lastXWorldSpace - 16) {
                levelStreamCommand = STREAM_LEVEL_LEFT;
                if (lastDeltaScroll > 0) {
                    levelStreamCommand = levelStreamCommand | STREAM_LEVEL_SWAP;
                    hunk_remaining = LevelDataLengths[level_data_index] - hunk_remaining;
                    for (auto i = 0; i < viewport_mx() * levelHeight + 2 * levelHeight; i++) {
                        GetPrevMetaTile();
                    }
                }

                lastDeltaScroll = deltaX;
                lastXWorldSpace = xWorldSpace;
                PopulateFromProvider(TileBuffer, 55,  GetPrevWrite, 28, -1);
                PopulateFromProvider(TileBuffer, 27, GetCurrentPrev, 28, -1);
                levelStreamCommand = levelStreamCommand | STREAM_LEVEL_DONE;
            }
        }
    } else {
        const oam::oam_t lastPlayerX = playerX;
        playerX += deltaX;
        if (deltaX > 0) {
            if (playerX > playerMaxXPos) {
                playerX = playerMaxXPos;
            }
        } else {
            if (playerX > lastPlayerX) {
                playerX = 0;
            }
        }

        oam::PopulateFromProvider(OAMBuffer, 1, oam::x, AdjustSpriteX, 8);
    }
}