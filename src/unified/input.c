#include <stdint.h>
#include <SDL3/SDL.h>
#include "../../include/platform-nes/input.h"
#include "internal.h"

static SDL_Gamepad *gamepads[2] = {0};

void input_init(void) {
    SDL_Init(SDL_INIT_GAMEPAD);
}

void input_handle_event(SDL_Event *e) {
    if (e->type == SDL_EVENT_GAMEPAD_ADDED) {
        for (int i = 0; i < 2; i++) {
            if (!gamepads[i]) {
                gamepads[i] = SDL_OpenGamepad(e->gdevice.which);
                SDL_Log("Controller Connected: %d", e->gdevice.which);
                break;
            }
        }
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