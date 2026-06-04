#include <platform-nes>
#include "main.hpp"
#include "graphics.hpp"
#include "levels.hpp"
#include "metasprites.hpp"
#include "handlers.hpp"
#include "colors.hpp"

u8 port1;
u8 port2;

oam::oam_t playerX;
oam::oam_t playerY;

i8 lastDeltaScroll;

u16 levelSize;
atomic u16 xWorldSpace;
atomic u16 lastXWorldSpace;

atomic u8 spriteZeroHandled;

oam::sprite_t OAMBuffer[64] __attribute__((aligned(256)));

static oam::oam_t Clear(u16 _);
static bool    BuildLevelSize();

RESET {
    if (!BuildLevelSize()) {
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

    hunk_remaining = LevelDataLengths[0];
    for (auto i = 0; i < 2 + video::viewport_tx(); i += 2) {
        ppu::WriteFromProviderToNameTable(
            i, 2,
            GetNextWrite, 28, 1
        );

        ppu::WriteFromProviderToNameTable(
            i + 1, 2,
            GetCurrentNext, 28, 1
        );

        ppu::WriteFromBufferToAttributeTable(i & ~3, 2, AttributeBuffer, 8, 1);
    }

    ppu::SetScroll(0, 0);

    AudioInit();
    TrackPlay(0);
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

    if (levelStreamCommand & STREAM_LEVEL_DONE) VRAM {
        if (levelStreamCommand & STREAM_LEVEL_RIGHT) {
            ppu::WriteFromBufferToNameTable((lastXWorldSpace >> 3) + video::viewport_tx() + 0, 2, TileBuffer, 28, 1);
            ppu::WriteFromBufferToNameTable((lastXWorldSpace >> 3) + video::viewport_tx() + 1, 2, TileBuffer + 28, 28, 1);
            if (!(levelStreamCommand & STREAM_LEVEL_SWAP))
                ppu::WriteFromBufferToAttributeTable((lastXWorldSpace >> 3) + video::viewport_tx() & ~3, 2, AttributeBuffer, 8, 1);
        } else {
            ppu::WriteFromBufferToNameTable((lastXWorldSpace >> 3) - 1, 2, TileBuffer, 28, 1);
            ppu::WriteFromBufferToNameTable((lastXWorldSpace >> 3) - 2, 2, TileBuffer + 28, 28, 1);
            if (!(levelStreamCommand & STREAM_LEVEL_SWAP))
                ppu::WriteFromBufferToAttributeTable((lastXWorldSpace >> 3) - 2 & ~3, 2, AttributeBuffer, 8, 1);
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

MINSIZE static bool BuildLevelSize() {
    u8 temp   = 0;
    levelSize = 0;

    for (u16 i = 0; i < 0xffff; i++) {
        if (LevelDataLengths[i] == 0)
            return true;

        temp += LevelDataLengths[i];

        while (temp >= levelHeight) {
            levelSize++;
            temp -= levelHeight;
        }
    }

    return false;
}