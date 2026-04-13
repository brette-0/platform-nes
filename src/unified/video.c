#include "../../include/platform-nes/video.h"
#include "../unified/internal.h"
#include <stdint.h>
#include <SDL3/SDL.h>

uint16_t xScroll;
uint16_t yScroll;

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
    // TODO: write this
}

static void toggle_fullscreen(void) {
    const uint32_t flags = SDL_GetWindowFlags(window);

    if (flags & SDL_WINDOW_FULLSCREEN) {
        SDL_SetWindowFullscreen(window, 0); // back to windowed
    } else {
        SDL_SetWindowFullscreen(window, SDL_WINDOW_FULLSCREEN);
    }
}

void WaitForPresent()
{
    SDL_RenderPresent(renderer);

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
    SDL_RenderClear(renderer);
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