#include "../include/video.h"
#include "../unified/internal.h"
#include <stdint.h>
#include <SDL3/SDL.h>

uint32_t vblank_tick(void *userdata, SDL_TimerID id, uint32_t interval) {
    atomic_store(&_vblank_flag, 1);
    return interval;  // repeat every 16ms
}

static uint64_t last_frame;

void WaitForPresent()
{
    SDL_RenderPresent(renderer);

    // pump events once
    SDL_Event e;
    while (SDL_PollEvent(&e)) {
        if (e.type == SDL_EVENT_QUIT) quit = 1;
        input_handle_event(&e);
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