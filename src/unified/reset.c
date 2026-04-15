#define SDL_MAIN_HANDLED

#include <SDL3/SDL.h>
#include <stdatomic.h>
#include <stdlib.h>
#include <assert.h>
#include <platform-nes/reset.h>
#include <platform-nes/video.h>
#include "../unified/internal.h"

SDL_Window *window;
SDL_Renderer *renderer;
SDL_TimerID timer_id;
atomic_int _vblank_flag;
void (*_nmi_callback)(void);
int quit;
const SDL_DisplayMode *mode;
uint8_t scale;

const uint8_t *patternTable = CHR_ROM;
uint8_t* VideoRAM;

void init() {
    if (!SDL_Init(SDL_INIT_VIDEO | SDL_INIT_GAMEPAD | SDL_INIT_AUDIO)) {
        SDL_Log("SDL_Init failed: %s", SDL_GetError());
        return;
    }

    const SDL_DisplayID display = SDL_GetPrimaryDisplay();
    mode = SDL_GetCurrentDisplayMode(display);

    paletteRAM = malloc(32);

    oamBuffer.data  = calloc(64, sizeof(struct sprite_t));
    oamBuffer.cap   = oamBuffer.data ? 64 : 0;
    oamBuffer.count = 0;
    sOAM            = 0;

    if (!oamBuffer.data) {
#ifndef NDEBUG
        assert(0 && "oamBuffer initial allocation failed");
#else
        oamBuffer.data = calloc(64, sizeof(struct sprite_t));
        oamBuffer.cap  = oamBuffer.data ? 64 : 0;
        SDL_Log("oamBuffer re-init %s", oamBuffer.data ? "recovered" : "FAILED");
#endif
    }

#ifdef LANDSCAPE
    scale = mode->h / 240;
    VideoRAM = malloc(

        mode->w / scale < 512 ? 0x800 : mode->w / scale * 0x400
    );
#endif
#if PORTRAIT
    scale = mode->w / 256;
    VideoRAM = malloc(
        mode->h / scale < 480 ? 0x800 : mode->w / scale * 0x400
    );
#endif


    if (!SDL_CreateWindowAndRenderer("My Game", mode->w >> 1, mode->h >> 1, 0, &window, &renderer)) {
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