#define SDL_MAIN_HANDLED

#include <SDL3/SDL.h>
#include <stdatomic.h>
#include "../../include/reset.h"
#include "../../include/audio.h"
#include "../unified/internal.h"

SDL_Window *window;
SDL_Renderer *renderer;
SDL_TimerID timer_id;
atomic_int _vblank_flag;
void (*_nmi_callback)(void);
int quit;

void init() {
    if (!SDL_Init(SDL_INIT_VIDEO | SDL_INIT_GAMEPAD | SDL_INIT_AUDIO)) {
        SDL_Log("SDL_Init failed: %s", SDL_GetError());
        return;
    }

    if (!SDL_CreateWindowAndRenderer("My Game", 3840, 2160, 0, &window, &renderer)) {
        SDL_Log("Window creation failed: %s", SDL_GetError());
        return;
    }
    timer_id = SDL_AddTimer(16, vblank_tick, NULL);
}

void post() {
    SDL_RemoveTimer(timer_id);
    SDL_DestroyRenderer(renderer);
    SDL_DestroyWindow(window);
    SDL_Quit();
}