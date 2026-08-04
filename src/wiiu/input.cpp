/**
 * @file input.cpp
 * @brief Controller polling for the native Wii U backend (SDL2 GameController).
 *
 * The wiiu portlib SDL2 exposes the GamePad (DRC), Pro Controllers and Wiimotes
 * through the standard SDL_GameController API, so this maps them onto the same
 * NES button bitmask the rest of the engine expects (see ::Buttons in
 * input.hpp) -- identical layout to the SDL3 desktop backend. Two players are
 * wired (the first two opened controllers); absent controllers read as idle.
 *
 * A/B map to the face buttons (SDL's SOUTH/EAST), Back/Start to Select/Start,
 * and the D-pad to the four directions.
 */
#include "internal.hpp"
#include <platform-nes/input.hpp>

#include <SDL2/SDL.h>

static SDL_GameController *pads[2] = { nullptr, nullptr };

void input_init() {
    int opened = 0;
    for (int i = 0; i < SDL_NumJoysticks() && opened < 2; i++) {
        if (SDL_IsGameController(i)) {
            pads[opened] = SDL_GameControllerOpen(i);
            if (pads[opened]) opened++;
        }
    }
}

static u8 read_pad(SDL_GameController *gc) {
    if (!gc) return 0;

    u8 state = 0;
    if (SDL_GameControllerGetButton(gc, SDL_CONTROLLER_BUTTON_A))          state |= input::A;
    if (SDL_GameControllerGetButton(gc, SDL_CONTROLLER_BUTTON_B))          state |= input::B;
    if (SDL_GameControllerGetButton(gc, SDL_CONTROLLER_BUTTON_BACK))       state |= input::SELECT;
    if (SDL_GameControllerGetButton(gc, SDL_CONTROLLER_BUTTON_START))      state |= input::START;
    if (SDL_GameControllerGetButton(gc, SDL_CONTROLLER_BUTTON_DPAD_UP))    state |= input::UP;
    if (SDL_GameControllerGetButton(gc, SDL_CONTROLLER_BUTTON_DPAD_DOWN))  state |= input::DOWN;
    if (SDL_GameControllerGetButton(gc, SDL_CONTROLLER_BUTTON_DPAD_LEFT))  state |= input::LEFT;
    if (SDL_GameControllerGetButton(gc, SDL_CONTROLLER_BUTTON_DPAD_RIGHT)) state |= input::RIGHT;
    return state;
}

void input::PollControllers(u8 *port1, u8 *port2) {
    SDL_GameControllerUpdate();
    *port1 = read_pad(pads[0]);
    *port2 = read_pad(pads[1]);
}
