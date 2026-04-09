#ifndef INTERNAL_H
#define INTERNAL_H

#include <SDL3/SDL.h>
#include <stdatomic.h>
extern SDL_Window *window;
extern SDL_Renderer *renderer;
extern SDL_TimerID timer_id;
extern atomic_int _vblank_flag;
extern void (*_nmi_callback)(void);
extern int quit;
extern void nmi(void);

uint32_t vblank_tick(void *userdata, SDL_TimerID id, uint32_t interval);
void input_handle_event(SDL_Event *e);

#endif