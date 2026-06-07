#include <platform-nes>
#include "main.hpp"
#include "graphics.hpp"
#include "level/levels.hpp"
#include "graphics/metasprites.hpp"
#include "colors.hpp"
#include "level/actor.hpp"

using namespace demo;

u8 port1;
u8 port2;

oam::oam_t playerX;
oam::oam_t playerY;

Actor player;

i8 lastDeltaScroll;

u16 levelSize;
atomic u16 xWorldSpace;
atomic u16 lastXWorldSpace;

atomic u8 spriteZeroHandled;

oam::sprite_t OAMBuffer[64] __attribute__((aligned(256)));

atomic u8 levelStreamCommand;
u8 TileBuffer[56];

static oam::oam_t Clear(u16 _);
static void PlayerUpdate(Actor* self);
static void PlayerReset(Actor* self);

RESET {
    if (!level::LoadLevel(0)) {
        reset();    // spin reset on NES, exit on SDL3
    }
    ppu::Flush(0x24, 0x00);

    oam::PopulateFromProvider(OAMBuffer, 0, oam::y, Clear, 64);

    // fill in with mario metatiles
    oam::PopulateFromBuffer(OAMBuffer, 1, oam::tile, msMario, 8);
    oam::PopulateFromProvider(OAMBuffer, 1, oam::y, AdjustSpriteY, 8);
    oam::PopulateFromProvider(OAMBuffer, 1, oam::x, AdjustSpriteX, 8);

    ppu::pal::WriteFromBuffer(ppu::BG_0,         SIZED_OBJ(BGColours));
    ppu::pal::WriteFromBuffer(ppu::SPRITE_0 + 1, SIZED_OBJ(marioColors));
    ppu::WriteFromBufferToNameTable(video::viewport_tx() - sizeof(msg_mario), 0, SIZED_OBJ(msg_mario), 0);

    ppu::WriteSingleToNameTable(0, 1, 0x2e);
    OAMBuffer[0] = ( oam::sprite_t){
        .y = 8,
        .tile = 0xff,
        .attributes = 0,
        .x = 0
    };

    level::hunk_remaining = level::HunkLengths[0];
    for (auto i = 0; i < 2 + video::viewport_tx(); i += 2) {
        ppu::WriteFromProviderToNameTable(
            i, 2,
            level::GetNextWrite, 28, 1
        );

        ppu::WriteFromProviderToNameTable(
            i + 1, 2,
            level::GetCurrentNext, 28, 1
        );

        ppu::WriteFromBufferToAttributeTable(i & ~3, 2, level::AttributeBuffer, 8, 1);
    }

    ppu::SetScroll(0, 0);

    AudioInit();
    TrackPlay(0);

    player.cursor.base     = TileData;
    player.cursor.offset   = 0;
    player.cursor.progress = 0;

    player.size = {
        16, 16
    };

    player.start = PlayerReset;

    player.Start();
    oam::RefreshSprites(OAMBuffer);   /* seed the first frame's sprite snapshot */
    ppu::EnableRendering(ppu::ctrl::BG_ADDR, ppu::mask::BG_L | ppu::mask::SPRITE_L);
    // ReSharper disable once CppDFAEndlessLoop
    while (!quit) {
        if (port1 & START) {
#ifndef  TARGET_NES
            quit = 1;
#endif

        }

        WaitThenReactToSpriteZero(0, 16, SpriteZeroHandler, &spriteZeroHandled);

        video::WaitForPresent();
    }
}

NMI {
    ppu::SetColorPriority(ppu::mask::BLUE);
    oam::RefreshSprites(OAMBuffer);

    spriteZeroHandled = 0;

    if (levelStreamCommand & STREAM_LEVEL_DONE) SHADOW(ppu::PPUMASK) {
        ppu::PPUMASK = 0;
        if (levelStreamCommand & STREAM_LEVEL_RIGHT) {
            ppu::WriteFromBufferToNameTable((lastXWorldSpace >> 3) + video::viewport_tx() + 0, 2, TileBuffer, 28, 1);
            ppu::WriteFromBufferToNameTable((lastXWorldSpace >> 3) + video::viewport_tx() + 1, 2, TileBuffer + 28, 28, 1);
            if (!(levelStreamCommand & STREAM_LEVEL_SWAP))
                ppu::WriteFromBufferToAttributeTable((lastXWorldSpace >> 3) + video::viewport_tx() & ~3, 2, level::AttributeBuffer, 8, 1);
        } else {
            ppu::WriteFromBufferToNameTable((lastXWorldSpace >> 3) - 1, 2, TileBuffer, 28, 1);
            ppu::WriteFromBufferToNameTable((lastXWorldSpace >> 3) - 2, 2, TileBuffer + 28, 28, 1);
            if (!(levelStreamCommand & STREAM_LEVEL_SWAP))
                ppu::WriteFromBufferToAttributeTable((lastXWorldSpace >> 3) - 2 & ~3, 2, level::AttributeBuffer, 8, 1);
        }
    }

    ppu::SetScroll(0, 0);
    if (levelStreamCommand & STREAM_LEVEL_DONE) {
        levelStreamCommand = 0;
    }

    ppu::SetColorPriority(0);
}

static oam::oam_t Clear(const u16 _) {
    return 0xef;
}

oam::oam_t AdjustSpriteY(const u16 i) {
    return playerY + (i >> 1) * 8;
}

oam::oam_t AdjustSpriteX(const u16 i) {
    return playerX + (i & 1) * 8;
}

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
        && ((deltaX > 0 && xWorldSpace + deltaX <= (level::nColumns - viewport_mx()) << 4)
            | (xWorldSpace + deltaX < xWorldSpace));

    if (couldScroll) {
        if (deltaX > 0) xWorldSpace = xWorldSpace + 1;
        else            xWorldSpace = xWorldSpace - 1;

        if (!(xWorldSpace & 0x0f)) {
            if (deltaX > 0) {
                if (xWorldSpace > lastXWorldSpace && xWorldSpace != (level::nColumns - viewport_mx()) << 4) {
                    levelStreamCommand = STREAM_LEVEL_RIGHT;
                    if (lastDeltaScroll < 0) {
                        levelStreamCommand = levelStreamCommand | STREAM_LEVEL_SWAP;
                        level::hunk_remaining = level::HunkLengths[level::level_data_index] - level::hunk_remaining;
                        for (auto i = 0; i < viewport_mx() * level::levelHeight + 2 * level::levelHeight; i++) {
                            level::GetNextMetaTile();
                        }
                    }


                    PopulateFromProvider(TileBuffer, 0,  level::GetNextWrite,   28, 1);
                    PopulateFromProvider(TileBuffer, 28, level::GetCurrentNext, 28, 1);
                    lastDeltaScroll = deltaX;
                    lastXWorldSpace = xWorldSpace;
                    levelStreamCommand = levelStreamCommand | STREAM_LEVEL_DONE;
                }
            } else if (xWorldSpace == lastXWorldSpace - 16) {
                levelStreamCommand = STREAM_LEVEL_LEFT;
                if (lastDeltaScroll > 0) {
                    levelStreamCommand = levelStreamCommand | STREAM_LEVEL_SWAP;
                    level::hunk_remaining = level::HunkLengths[level::level_data_index] - level::hunk_remaining;
                    for (auto i = 0; i < viewport_mx() * level::levelHeight + 2 * level::levelHeight; i++) {
                        level::GetPrevMetaTile();
                    }
                }

                lastDeltaScroll = deltaX;
                lastXWorldSpace = xWorldSpace;
                PopulateFromProvider(TileBuffer, 55,  level::GetPrevWrite,   28, -1);
                PopulateFromProvider(TileBuffer, 27,  level::GetCurrentPrev, 28, -1);
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

void PlayerUpdate(Actor* self) {

}

void PlayerReset(Actor* self) {
    // Stream the cursor from the level start, then walk it to the actor's
    // current metatile.  Column-major: index = col*levelHeight + row.
    self->cursor.base     = TileData;
    self->cursor.offset   = 0;
    self->cursor.progress = 0;

    const u16 y   = self->worldSpace.y.coarse;
    const i16 col = static_cast<i16>(self->worldSpace.x.coarse >> 4);
    const i16 row = (y & 0x8000)                                   ? 0                      // above the field -> top row
                  : (static_cast<i16>(y >> 4) < level::levelHeight ? static_cast<i16>(y >> 4)
                                                                   : level::levelHeight - 1); // below the floor -> bottom row

    if (const i16 amt = col * level::levelHeight + row) self->cursor.Move(amt);
}