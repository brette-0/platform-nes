#ifndef INTERNAL_H
#define INTERNAL_H

#include <atomic>
#include <SDL3/SDL.h>
#include <intsh>
using namespace br0::intsh;

extern SDL_Window *window;
extern SDL_Renderer *renderer;
extern SDL_TimerID timer_id;
extern std::atomic_int _vblank_flag;
extern void (*_nmi_callback)();
extern int quit;
extern void nmi_vector();

u32 vblank_tick(void *userdata, SDL_TimerID id, u32 interval);
void input_handle_event(const SDL_Event *e);

#endif