#include "../../include/platform-nes/video.h"
#include "../unified/internal.h"
#include <stdint.h>
#include <SDL3/SDL.h>

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