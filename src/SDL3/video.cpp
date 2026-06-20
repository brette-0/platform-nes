/**
 * @file video.cpp
 * @brief SDL3 presentation backend for the shared software PPU.
 *
 * The PPU emulation itself -- ::emu::GenerateFrame and every
 * nametable/attribute/palette/OAM write function plus the scroll and
 * sprite-zero machinery -- lives in src/emu/ppu.cpp and is shared verbatim with
 * the libogc/GX backend. This file is only the desktop-specific half: it opens
 * the window, paces frames off an SDL timer, locks a streaming texture, asks the
 * core to render one frame into it, and lets SDL's renderer scale it to the
 * window with nearest-neighbour filtering.
 */
#include "../SDL3/internal.hpp"

#include <platform-nes/video.hpp>
#include <platform-nes/interrupts.hpp>
#include "../emu/emu.hpp"

#include <cstdlib>
#include <atomic>
#include <SDL3/SDL.h>

#ifdef __EMSCRIPTEN__
// On the web the browser owns the event loop; emscripten_sleep() (with the
// -sASYNCIFY link flag set by the web build) suspends the C stack and returns
// control to requestAnimationFrame, then resumes here next tick. This is what
// lets the otherwise-blocking main loop run unchanged in a browser.
#include <emscripten.h>
#endif

// ---------------------------------------------------------------------------
// SDL-specific state. Everything the core needs (VideoRAM, paletteRAM, scroll,
// PPUCTRL/PPUMASK, the OAM snapshot, patternTable, ...) is owned by src/emu;
// only the genuine SDL objects live here.
// ---------------------------------------------------------------------------
SDL_Window   *window;
SDL_Renderer *renderer;
SDL_TimerID   timer_id;
std::atomic_int    _vblank_flag;
void        (*_nmi_callback)();
const SDL_DisplayMode *mode;
u8       scale;

static SDL_Texture *bgTexture;
static u64          last_frame;

u32 vblank_tick(void *userdata, SDL_TimerID id, const u32 interval) {
    _vblank_flag = 1;
    return interval;  // repeat every 16ms
}

static void toggle_fullscreen() {
    if (const u32 flags = SDL_GetWindowFlags(window); flags & SDL_WINDOW_FULLSCREEN) {
        SDL_SetWindowFullscreen(window, false); // back to windowed
    } else {
        SDL_SetWindowFullscreen(window, SDL_WINDOW_FULLSCREEN);
    }
}

// Render one frame through the shared core into the streaming texture, then
// blit it to the window. The core fills ARGB8888 pixels; SDL scales the
// fixed NES surface up with SCALEMODE_NEAREST, matching the GX backend's quad.
static void present_frame() {
    const int vpw = video::viewport_px();
    const int vph = video::viewport_py();

    if (!bgTexture) {
        bgTexture = SDL_CreateTexture(renderer, SDL_PIXELFORMAT_ARGB8888,
                                      SDL_TEXTUREACCESS_STREAMING, vpw, vph);
        if (!bgTexture) return;
        SDL_SetTextureScaleMode(bgTexture, SDL_SCALEMODE_NEAREST);
    }

    void *raw; int pitch;
    if (!SDL_LockTexture(bgTexture, nullptr, &raw, &pitch)) return;
    emu::GenerateFrame(static_cast<u32 *>(raw), pitch / 4);
    SDL_UnlockTexture(bgTexture);

    SDL_RenderTexture(renderer, bgTexture, nullptr, nullptr);
}

namespace video {

void WaitForPresent() {
    // pump events once
    SDL_Event e;
    while (SDL_PollEvent(&e)) {

        switch (e.type) {

        case SDL_EVENT_QUIT:
            quit = 1;
            break;

        case SDL_EVENT_KEY_DOWN: {
                const SDL_Keycode key = e.key.key;

                if (const SDL_Keymod mod = SDL_GetModState(); key == SDLK_RETURN && (mod & SDL_KMOD_ALT)) {
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

    constexpr u64 target = 16666667;   // 60 Hz frame budget, ns
#ifdef __EMSCRIPTEN__
    // Must yield to the browser every frame (not busy-wait): emscripten_sleep
    // hands control back to the event loop so the canvas paints and input flows.
    // Round the remaining budget to whole ms (its resolution); always sleep at
    // least 1ms so a frame that overran its budget still yields rather than
    // spinning the tab. Asyncify resumes execution right here next tick.
    const u64 elapsed = SDL_GetTicksNS() - last_frame;
    const u64 remain  = elapsed < target ? target - elapsed : 0;
    emscripten_sleep(static_cast<unsigned>(remain / 1000000) + 1);
    last_frame = SDL_GetTicksNS();
#else
    const u64 elapsed = SDL_GetTicksNS() - last_frame;
    if (elapsed < target) {
        SDL_DelayPrecise(target - elapsed);
    }
    last_frame = SDL_GetTicksNS();
#endif

    if (ppu::PPUMASK & (ppu::mask::BG | ppu::mask::SPRITE)) {
        present_frame();
        SDL_RenderPresent(renderer);
    } else {
        SDL_RenderClear(renderer);
        SDL_RenderPresent(renderer);
    }
    /* No IRQs permitted post-frame; discard anything still queued
     * from this frame's render before NMI enqueues for the next one. */
    irqCount = 0;
    nmi();
}

}   // namespace video

void init() {
    if (!SDL_Init(SDL_INIT_VIDEO | SDL_INIT_GAMEPAD | SDL_INIT_AUDIO)) {
        SDL_Log("SDL_Init failed: %s", SDL_GetError());
        return;
    }

    const SDL_DisplayID display = SDL_GetPrimaryDisplay();
    mode = SDL_GetCurrentDisplayMode(display);

#ifdef LANDSCAPE
    scale = mode->h / 240;
    const unsigned vram = mode->w / scale < 512 ? 0x800 : mode->w / scale * 0x400;
#endif
#if PORTRAIT
    scale = mode->w / 256;
    const unsigned vram = mode->h / scale < 480 ? 0x800 : mode->w / scale * 0x400;
#endif

    // The core owns VideoRAM/paletteRAM; the VRAM size policy is the backend's
    // (window-derived on SDL, fixed two pages on OGC), so size it here.
    emu::InitMemory(vram);

    if (!SDL_CreateWindowAndRenderer(PROJECT_NAME, mode->w >> 1, mode->h >> 1, 0, &window, &renderer)) {
        SDL_Log("Window creation failed: %s", SDL_GetError());
        return;
    }
    timer_id = SDL_AddTimer(16, vblank_tick, nullptr);
}

void post() {
    SDL_RemoveTimer(timer_id);
    if (bgTexture) SDL_DestroyTexture(bgTexture);
    SDL_DestroyRenderer(renderer);
    SDL_DestroyWindow(window);
    SDL_Quit();
}
