#include "../../include/platform-nes/video.h"
#include "../SDL3/internal.h"
#include <platform-nes/interrupts.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <SDL3/SDL.h>

uint16_t xScroll;
uint16_t yScroll;
uint8_t* paletteRAM;
static uint8_t  ppuMask;
static SDL_Texture *bgTexture;
static uint8_t ppuCtrl;

extern const uint8_t *patternTable;

static const uint32_t nes_rgb[64] = {
    0xFF626262, 0xFF012090, 0xFF1B0CA4, 0xFF3B009E,
    0xFF520080, 0xFF5A004E, 0xFF521610, 0xFF3F2E00,
    0xFF234400, 0xFF0A5200, 0xFF005804, 0xFF004E30,
    0xFF003C62, 0xFF000000, 0xFF000000, 0xFF000000,
    0xFFABABAB, 0xFF1F56D8, 0xFF423CF2, 0xFF6E24EC,
    0xFF9218C4, 0xFF9E1A80, 0xFF933434, 0xFF7A5200,
    0xFF576E00, 0xFF2E8400, 0xFF118E0E, 0xFF008848,
    0xFF007898, 0xFF000000, 0xFF000000, 0xFF000000,
    0xFFFBFBFB, 0xFF6BA4FF, 0xFF8C88FF, 0xFFB87AFF,
    0xFFE072FF, 0xFFF076D0, 0xFFE88C78, 0xFFCCA830,
    0xFFA8C410, 0xFF7EDC24, 0xFF5AE84E, 0xFF48E490,
    0xFF48D4E0, 0xFF4E4E4E, 0xFF000000, 0xFF000000,
    0xFFFBFBFB, 0xFFBED4FF, 0xFFCACAFF, 0xFFDCC4FF,
    0xFFECC0FF, 0xFFF2C0EA, 0xFFF2C8C4, 0xFFE8D4A4,
    0xFFD8E09C, 0xFFC8EC9C, 0xFFBCF0AC, 0xFFB4F0CC,
    0xFFB4E8F0, 0xFFB8B8B8, 0xFF000000, 0xFF000000,
};



#ifdef _WIN32
__asm__(
    ".pushsection chr_rom$a,\"dr\"\n"
    ".global _chr_rom\n"
    "_chr_rom:\n"
    ".popsection\n"
);
#endif

uint32_t vblank_tick(void *userdata, SDL_TimerID id, uint32_t interval) {
    atomic_store(&_vblank_flag, 1);
    return interval;  // repeat every 16ms
}

static uint64_t last_frame;

void EnableRendering(uint8_t ppuCtrl_, uint8_t ppuMask_) {
    ppuMask = ppuMask_;
    ppuCtrl = ppuCtrl_;
}

static void toggle_fullscreen(void) {
    const uint32_t flags = SDL_GetWindowFlags(window);

    if (flags & SDL_WINDOW_FULLSCREEN) {
        SDL_SetWindowFullscreen(window, 0); // back to windowed
    } else {
        SDL_SetWindowFullscreen(window, SDL_WINDOW_FULLSCREEN);
    }
}

#pragma region PPU_EMU

/* Merged per-pixel renderer. Walks (px, py) in raster order, compositing
 * background + sprites at each pixel and applying PPUMASK flags (left-8
 * clips, greyscale, colour emphasis) uniformly to the final sample.
 *
 * IRQ dispatch is segmented: each scanline is split at the px of the next
 * queued IRQ. Within a segment we render straight through (no per-pixel
 * dispatch overhead, since no handler can fire); when the segment is
 * exhausted we invoke the handler and resume. A handler fires *before*
 * the pixel at its (px, py) renders, so it can mutate scroll / palette /
 * ppuCtrl and have that pixel onward see the new state. */
static void GenerateFrame() {
    const int vpw = VIEWPORT_X * 8;
    const int vph = VIEWPORT_Y * 8;

    if (!bgTexture) {
        bgTexture = SDL_CreateTexture(
            renderer, SDL_PIXELFORMAT_ARGB8888,
            SDL_TEXTUREACCESS_STREAMING, vpw, vph
        );
        if (!bgTexture) return;
        SDL_SetTextureScaleMode(bgTexture, SDL_SCALEMODE_NEAREST);
    }

    void *raw;
    int   pitch;
    if (!SDL_LockTexture(bgTexture, NULL, &raw, &pitch)) return;

    uint32_t *pixels = raw;
    const int stride = pitch / 4;

    size_t irq_idx = 0;

    const int nt_cols  = vpw < 512 ? 2 : (vpw + 255) / 256;
    const int world_w  = nt_cols * 256;
    const int spr_base = (ppuCtrl & SPRITE_ADDR) ? 0x1000 : 0x0000;

    for (int py = 0; py < vph; py++) {
        /* Pre-filter sprites that overlap this scanline. Keeps the inner
         * pixel loop from scanning all of OAM 256 times per row. */
        int line_spr[64];
        int n_line = 0;
        if ((ppuMask & SPRITE) && oamBuffer.data) {
            for (size_t s = 0; s < oamBuffer.count && n_line < 64; s++) {
                const int sy = (int)oamBuffer.data[s].y;
                if (py >= sy && py < sy + 8) line_spr[n_line++] = (int)s;
            }
        }

        /* Walk this scanline in segments bounded by IRQ positions. */
        int seg_start = 0;
        while (seg_start < vpw) {
            /* Discard any IRQs already behind the cursor, then peek the
             * head to find the next stop on this scanline (if any). */
            int seg_end = vpw;
            int fire    = 0;
            while (irq_idx < irqCount) {
                const irq_t ev = irqBuffer[irq_idx];
                if ((int)ev.py < py
                    || ((int)ev.py == py && (int)ev.px < seg_start)) {
                    irq_idx++;
                    continue;
                }
                if ((int)ev.py == py) {
                    seg_end = (int)ev.px;
                    fire    = 1;
                }
                break;
            }

            for (int px = seg_start; px < seg_end; px++) {
                /* --- Background sample -------------------------------- */
                int     bg_cidx = 0;
                uint8_t bg_pal  = 0;
                const int bg_left_ok = (ppuMask & 0x02) || px >= 8;
                if ((ppuMask & BG) && bg_left_ok) {
                    const int wy        = ((int)yScroll + py) % 240;
                    const int tile_row  = wy / 8;
                    const int local_row = tile_row % 30;
                    const int nt_row    = tile_row / 30;
                    const int fine_y    = wy & 7;

                    const int wx        = ((int)xScroll + px) % world_w;
                    const int tile_col  = wx / 8;
                    const int local_col = tile_col % 32;
                    const int nt_col    = tile_col / 32;
                    const int fine_x    = wx & 7;

                    const int nt_off = (nt_col + nt_row * nt_cols) * 0x400;

                    const uint8_t tile_id =
                        VideoRAM[nt_off + local_row * 32 + local_col];
                    const uint8_t attr =
                        VideoRAM[nt_off + 0x3C0
                                 + (local_row / 4) * 8
                                 + (local_col / 4)];
                    const int shift = ((local_col >> 1) & 1) * 2
                                    + ((local_row >> 1) & 1) * 4;
                    bg_pal = (attr >> shift) & 3;

                    const int chr_addr = ((ppuCtrl & BG_ADDR) ? 0x1000 : 0)
                                       + tile_id * 16 + fine_y;
                    const uint8_t lo = patternTable[chr_addr];
                    const uint8_t hi = patternTable[chr_addr + 8];
                    const int bit  = 7 - fine_x;
                    bg_cidx = ((lo >> bit) & 1) | (((hi >> bit) & 1) << 1);
                }
                const int bg_opaque = bg_cidx != 0;

                /* --- Sprite sample ------------------------------------ */
                /* Low OAM index = higher priority, so walk forward and
                 * stop at the first opaque hit. */
                int     spr_hit    = 0;
                int     spr_behind = 0;
                uint8_t spr_nes    = 0;
                const int spr_left_ok = (ppuMask & 0x04) || px >= 8;
                if ((ppuMask & SPRITE) && spr_left_ok) {
                    for (int k = 0; k < n_line; k++) {
                        const struct sprite_t spr = oamBuffer.data[line_spr[k]];
                        const int sx = (int)spr.x;
                        if (px < sx || px >= sx + 8) continue;
                        const int sy = (int)spr.y;
                        const uint8_t attr = spr.attributes;
                        const int flip_h = attr & 0x40;
                        const int flip_v = attr & 0x80;
                        const int row     = flip_v ? (7 - (py - sy)) : (py - sy);
                        const int col_bit = flip_h ? (px - sx) : (7 - (px - sx));
                        const int addr = spr_base + spr.tile * 16 + row;
                        const uint8_t lo = patternTable[addr];
                        const uint8_t hi = patternTable[addr + 8];
                        const int cidx = ((lo >> col_bit) & 1)
                                       | (((hi >> col_bit) & 1) << 1);
                        if (cidx == 0) continue;
                        spr_nes    = paletteRAM[0x10 + (attr & 0x03) * 4 + cidx];
                        spr_behind = attr & 0x20;
                        spr_hit    = 1;
                        break;
                    }
                }

                /* --- Compose ------------------------------------------ */
                uint8_t final_nes;
                if (spr_hit && (!spr_behind || !bg_opaque)) {
                    final_nes = spr_nes;
                } else if (bg_opaque) {
                    final_nes = paletteRAM[bg_pal * 4 + bg_cidx];
                } else {
                    final_nes = paletteRAM[0]; /* universal */
                }

                /* Greyscale forces colour to the column-0 shades. */
                if (ppuMask & 0x01) final_nes &= 0x30;

                uint32_t col = nes_rgb[final_nes & 0x3F];

                /* Colour emphasis: set bit = that channel stays full,
                 * the others attenuate. Approximate with a 3/4 scale. */
                if (ppuMask & 0xE0) {
                    uint32_t r = (col >> 16) & 0xFF;
                    uint32_t g = (col >>  8) & 0xFF;
                    uint32_t b =  col        & 0xFF;
                    if (ppuMask & 0x20) { g = g * 3 / 4; b = b * 3 / 4; }
                    if (ppuMask & 0x40) { r = r * 3 / 4; b = b * 3 / 4; }
                    if (ppuMask & 0x80) { r = r * 3 / 4; g = g * 3 / 4; }
                    col = 0xFF000000u | (r << 16) | (g << 8) | b;
                }

                pixels[py * stride + px] = col;
            }

            /* Segment exhausted — fire handler before the next segment
             * begins, so the pixel at seg_end sees the new state. */
            if (fire) {
                const irq_t ev = irqBuffer[irq_idx];
                if (ev.id < irqTableCount && irqTable[ev.id]) {
                    irqTable[ev.id]();
                }
                irq_idx++;
            }

            seg_start = seg_end;
        }
    }

    SDL_UnlockTexture(bgTexture);
    SDL_RenderTexture(renderer, bgTexture, NULL, NULL);
}

#pragma endregion




void WaitForPresent() {
    // pump events once
    SDL_Event e;
    while (SDL_PollEvent(&e)) {

        switch (e.type) {

        case SDL_EVENT_QUIT:
            quit = 1;
            break;

        case SDL_EVENT_KEY_DOWN: {
                SDL_Keycode key = e.key.key;

                SDL_Keymod mod = SDL_GetModState();

                if (key == SDLK_RETURN && (mod & SDL_KMOD_ALT)) {
                    toggle_fullscreen();
                }

                if (key == SDLK_F11) {
                    toggle_fullscreen();
                }

                input_handle_event(&e);
                break;
        }

        default:
            input_handle_event(&e);
            break;
        }
    }

    uint64_t elapsed = SDL_GetTicksNS() - last_frame;
    uint64_t target = 16666667;
    if (elapsed < target) {
        SDL_DelayPrecise(target - elapsed);
    }
    last_frame = SDL_GetTicksNS();

    if (ppuMask & (BG | SPRITE)) {
        GenerateFrame();
        SDL_RenderPresent(renderer);
    } else {
        SDL_RenderClear(renderer);
        SDL_RenderPresent(renderer);
    }
    nmi();
    /* No IRQs permitted post-frame; discard anything still queued. */
    irqCount = 0;
}

void FlushVideoRAM(const uint8_t nt, const uint8_t at) {

    for (uint16_t page = 0;
        mode->w / scale < 512 ? page < 2 : page < mode->w / scale;
        page++
    ) {
        for (uint16_t i = 0; i < 0x3c0; i++) {
            VideoRAM[page * 0x400 + i] = nt;
        }

        for (uint16_t i = 0; i < 0x40; i++) {
            VideoRAM[page * 0x400 + 0x3c0 + i] = at;
        }
    }
}

inline static uint16_t xy_to_nt_addr(uint16_t x, uint16_t y) {
    uint16_t nt_h = x / 32;
    uint16_t nt_v = y / 30;
    uint16_t col  = x % 32;
    uint16_t row  = y % 30;

    return (nt_h + nt_v * (VIEWPORT_X < 64 ? 2 : (VIEWPORT_X + 31) / 32)) * 0x400 + row * 32 + col;
}

inline static uint16_t xy_to_at_addr(uint16_t x, uint16_t y) {
    uint16_t nt_h = x / 32;
    uint16_t nt_v = y / 30;
    uint16_t col  = x % 32;
    uint16_t row  = y % 30;

    return (nt_h + nt_v * (VIEWPORT_X < 64 ? 2 : (VIEWPORT_X + 31) / 32)) * 0x400
         + 0x3C0 + (row / 4) * 8 + (col / 4);
}

void WriteBufferToVideoMemory(
    const uint16_t x, const uint16_t y, const uint8_t* source, const uint8_t sBuffer, uint8_t polarity
) {
    const uint16_t offset = xy_to_nt_addr(x, y);
    memcpy(VideoRAM + offset, source, sBuffer);
}

void SetScroll(uint16_t x, uint16_t y) {
    xScroll = x; yScroll = y;
}

void DeltaScroll(int8_t x, int8_t y) {
    xScroll = (uint16_t)(xScroll + x);
    yScroll = (uint16_t)(yScroll + y);
}

void WriteBufferToPaletteMemory(const uint8_t offset, const uint8_t* source, const uint8_t sBuffer) {
    memcpy(paletteRAM + offset, source, sBuffer);
}

void WriteProviderToVideoMemory(
    const uint16_t x, const uint16_t y, uint8_t (*fn)(uint8_t), const uint8_t amt, const uint8_t polarity
) {
    ppuCtrl &= ~POLARITY;
    if (polarity) ppuCtrl |= POLARITY;

    const uint16_t offset = xy_to_nt_addr(x, y);
    for (uint8_t i = 0; i < amt; i++) {
        VideoRAM[offset + i * (ppuCtrl & POLARITY ? 32 : 1)] = fn(i);
    }
}

void WriteBufferToAttributeMemory(
    const uint16_t x, const uint16_t y, const uint8_t* source, const uint8_t sBuffer, uint8_t polarity
) {
    const uint16_t offset = xy_to_at_addr(x, y);
    for (uint8_t i = 0; i < sBuffer; i++) {
        VideoRAM[offset + i * (ppuCtrl & POLARITY ? 8 : 1)] = source[i];
    }
}

oamBuffer_t oamBuffer = { NULL, 0, 0 };
size_t      sOAM      = 0;

static void oam_grow_to(size_t sprites) {
    if (sprites <= oamBuffer.cap) return;
    size_t n = oamBuffer.cap ? oamBuffer.cap * 2 : 64;
    while (n < sprites) n *= 2;
    oamBuffer.data = realloc(oamBuffer.data, n * sizeof(struct sprite_t));
    oamBuffer.cap  = n;
}

/* Bytes needed to hold `count` strided writes with stride `step`. */
static size_t strided_bytes(uint16_t count, uint16_t step) {
    if (count == 0) return 0;
    return (size_t)(count - 1) * step + 1;
}

void OAMPopulateFromBuffer(uint16_t offset, const uint8_t* buffer, uint16_t sBuffer, uint16_t step) {
    size_t last_byte = (size_t)offset + strided_bytes(sBuffer, step);
    size_t sprites   = (last_byte + sizeof(struct sprite_t) - 1) / sizeof(struct sprite_t);
    oam_grow_to(sprites);
    uint8_t* dst = (uint8_t*)oamBuffer.data + offset;
    for (uint16_t i = 0; i < sBuffer; i++) dst[i * step] = buffer[i];
    if (sprites > oamBuffer.count) { oamBuffer.count = sprites; sOAM = sprites; }
}

void OAMPopulateFromProvider(uint16_t offset, uint8_t (*fn)(uint16_t), uint16_t amt, uint16_t step) {
    size_t last_byte = (size_t)offset + strided_bytes(amt, step);
    size_t sprites   = (last_byte + sizeof(struct sprite_t) - 1) / sizeof(struct sprite_t);
    oam_grow_to(sprites);
    uint8_t* dst = (uint8_t*)oamBuffer.data + offset;
    for (uint16_t i = 0; i < amt; i++) dst[i * step] = fn(i);
    if (sprites > oamBuffer.count) { oamBuffer.count = sprites; sOAM = sprites; }
}

// link happy stub
void RefreshSprites(void) { }