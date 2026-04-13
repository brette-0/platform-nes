#include "../../include/platform-nes/video.h"
#include "../unified/internal.h"
#include <stdint.h>
#include <SDL3/SDL.h>

uint16_t xScroll;
uint16_t yScroll;

extern const uint8_t *patternTable;

/* NES 2C02 NTSC master palette — 64 entries, ARGB8888 */
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

/*
 * Palette RAM — 32 bytes, same layout as PPU $3F00-$3F1F.
 * Bytes  0-15: four background palettes (4 colours each).
 * Bytes 16-31: four sprite palettes.
 * Default is greyscale so something is visible out of the box.
 */
uint8_t* paletteRAM;

static uint8_t  renderMask;
static SDL_Texture *bgTexture;

/*
 * PE/COFF: anchor _chr_rom at the very start of the chr_rom section.
 * The library emits into chr_rom$a; CHARACTER_ROM() emits into chr_rom$m.
 * The linker merges them alphabetically by suffix, so $a is always first.
 *
 * ELF / Mach-O don't need this — the linker provides the boundary
 * symbol automatically (__start_chr_rom / section$start$...).
 */
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

void EnableRendering(uint8_t ppuMask) {
    renderMask = ppuMask;
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

static void GenerateBackground() {
    const int vpw = VIEWPORT_X * 8;
    const int vph = VIEWPORT_Y * 8;

    /* lazy-create the streaming texture */
    if (!bgTexture) {
        bgTexture = SDL_CreateTexture(
            renderer, SDL_PIXELFORMAT_ARGB8888,
            SDL_TEXTUREACCESS_STREAMING, vpw, vph
        );
        if (!bgTexture) return;
    }

    void *raw;
    int   pitch;
    if (!SDL_LockTexture(bgTexture, NULL, &raw, &pitch)) return;

    uint32_t *pixels = raw;
    const int stride = pitch / 4;

    /*
     * Nametable geometry.
     * Standard (viewport < 512 px): 2 NTs side-by-side  = 512 × 240.
     * Extended: one NT per 256 px of viewport width.
     */
    const int nt_cols = vpw < 512 ? 2 : (vpw + 255) / 256;
    const int world_w = nt_cols * 256;   /* pixels */

    for (int py = 0; py < vph; py++) {
        const int world_h = 240;
        const int wy        = ((int)yScroll + py) % world_h;
        const int tile_row  = wy / 8;
        const int local_row = tile_row % 30;
        const int nt_row    = tile_row / 30;
        const int fine_y    = wy & 7;

        for (int px = 0; px < vpw; px++) {
            const int wx        = ((int)xScroll + px) % world_w;
            const int tile_col  = wx / 8;
            const int local_col = tile_col % 32;
            const int nt_col    = tile_col / 32;
            const int fine_x    = wx & 7;

            /* VRAM offset for this nametable */
            const int nt_off = (nt_col + nt_row * nt_cols) * 0x400;

            /* tile index from the nametable */
            const uint8_t tile_id =
                VideoRAM[nt_off + local_row * 32 + local_col];

            /* attribute table: each byte covers a 4×4-tile region */
            const uint8_t attr =
                VideoRAM[nt_off + 0x3C0
                         + local_row / 4 * 8
                         + local_col / 4];
            const int shift = (local_col >> 1 & 1) * 2
                            + (local_row >> 1 & 1) * 4;
            const int pal = attr >> shift & 3;

            /* decode the CHR tile (two-bitplane NES format) */
            const int     chr_addr = tile_id * 16 + fine_y;
            const uint8_t lo = patternTable[chr_addr];
            const uint8_t hi = patternTable[chr_addr + 8];
            const int     bit  = 7 - fine_x;
            const int     cidx = lo >> bit & 1
                               | (hi >> bit & 1) << 1;

            /* colour 0 is always the universal background */
            const uint8_t nes_col =
                cidx ? paletteRAM[pal * 4 + cidx] : paletteRAM[0];

            pixels[py * stride + px] = nes_rgb[nes_col & 0x3F];
        }
    }

    SDL_UnlockTexture(bgTexture);

    const SDL_FRect dst = {
        0, 0, (float)(vpw * scale), (float)(vph * scale)
    };
    SDL_RenderTexture(renderer, bgTexture, NULL, &dst);
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

                // Alt + Enter toggle fullscreen
                if (key == SDLK_RETURN && (mod & SDL_KMOD_ALT)) {
                    toggle_fullscreen(); // your function
                }

                // F11 toggle fullscreen (common convention)
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

    // sleep the exact remaining time
    uint64_t elapsed = SDL_GetTicksNS() - last_frame;
    uint64_t target = 16666667;
    if (elapsed < target) {
        SDL_DelayPrecise(target - elapsed);
    }
    last_frame = SDL_GetTicksNS();

    nmi();
    if (renderMask & BG){
        GenerateBackground();
        SDL_RenderPresent(renderer);
    } else {
        SDL_RenderClear(renderer);
    }

}

void FlushVideoRAM(const uint8_t byte) {
    for (uint16_t i = 0;
        mode->w / scale < 512 ? i < 0x800 : i < mode->w / scale * 0x400;
        i++
    ) {
        VideoRAM[i] = byte;
    }
}

inline static uint16_t xy_to_nt_addr(uint16_t x, uint16_t y) {
    uint16_t nt_h = ((x >> 8) & 1) << 10;   // +$0400 if NT1/NT3
    uint16_t nt_v = (y / 30) << 11;          // +$0800 if NT2/NT3
    uint16_t col  = x & 0x1F;                // 0-31 tile column
    uint16_t row  = (y % 30);                // 0-29 tile row within NT

    return nt_h + nt_v + row * 32 + col;
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

void WriteBufferToPaletteMemory(const uint8_t offset, const uint8_t* source, const uint8_t sBuffer) {
    memcpy(paletteRAM + offset, source, sBuffer);
}