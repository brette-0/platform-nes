#include <stdint.h>
#include <SDL3/SDL.h>
#include <platform-nes/input.h>
#include "internal.h"

static SDL_Gamepad *gamepads[2] = {0};

static void open_gamepad_id(SDL_JoystickID which) {
    for (int i = 0; i < 2; i++) {
        if (!gamepads[i]) {
            gamepads[i] = SDL_OpenGamepad(which);
            if (gamepads[i]) {
                SDL_Log("Controller Connected: %u (%s)",
                        (unsigned)which, SDL_GetGamepadName(gamepads[i]));
            } else {
                SDL_Log("SDL_OpenGamepad(%u) failed: %s",
                        (unsigned)which, SDL_GetError());
            }
            return;
        }
    }
}

void input_init(void) {
    if (!SDL_Init(SDL_INIT_GAMEPAD)) {
        SDL_Log("SDL_INIT_GAMEPAD failed: %s", SDL_GetError());
        return;
    }

    int count = 0;
    SDL_JoystickID *ids = SDL_GetGamepads(&count);
    if (ids) {
        for (int i = 0; i < count; i++) open_gamepad_id(ids[i]);
        SDL_free(ids);
    }
}

void input_handle_event(SDL_Event *e) {
    if (e->type == SDL_EVENT_GAMEPAD_ADDED) {
        for (int i = 0; i < 2; i++) {
            if (gamepads[i] &&
                SDL_GetGamepadID(gamepads[i]) == e->gdevice.which) {
                return;
            }
        }
        open_gamepad_id(e->gdevice.which);
    }
    if (e->type == SDL_EVENT_GAMEPAD_REMOVED) {
        for (int i = 0; i < 2; i++) {
            if (gamepads[i] &&
                SDL_GetGamepadID(gamepads[i]) == e->gdevice.which) {
                SDL_CloseGamepad(gamepads[i]);
                gamepads[i] = NULL;
                break;
                }
        }
    }
}

static uint8_t read_gamepad(SDL_Gamepad *pad) {
    if (!pad) return 0;

    uint8_t state = 0;
    if (SDL_GetGamepadButton(pad, SDL_GAMEPAD_BUTTON_SOUTH))          state |= 0x01;  // A
    if (SDL_GetGamepadButton(pad, SDL_GAMEPAD_BUTTON_EAST))           state |= 0x02;  // B
    if (SDL_GetGamepadButton(pad, SDL_GAMEPAD_BUTTON_BACK))           state |= 0x04;  // Select
    if (SDL_GetGamepadButton(pad, SDL_GAMEPAD_BUTTON_START))          state |= 0x08;  // Start
    if (SDL_GetGamepadButton(pad, SDL_GAMEPAD_BUTTON_DPAD_UP))        state |= 0x10;  // Up
    if (SDL_GetGamepadButton(pad, SDL_GAMEPAD_BUTTON_DPAD_DOWN))      state |= 0x20;  // Down
    if (SDL_GetGamepadButton(pad, SDL_GAMEPAD_BUTTON_DPAD_LEFT))      state |= 0x40;  // Left
    if (SDL_GetGamepadButton(pad, SDL_GAMEPAD_BUTTON_DPAD_RIGHT))     state |= 0x80;  // Right

    return state;
}

void PollControllers(uint8_t* port1, uint8_t* port2) {
    *port1 = read_gamepad(gamepads[0]);
    *port2 = read_gamepad(gamepads[1]);
}