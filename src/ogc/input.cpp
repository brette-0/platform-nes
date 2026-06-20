/**
 * @file input.cpp
 * @brief Controller polling for the GameCube + Wii backend.
 *
 * Maps the two native input stacks onto the same NES button bitmask the rest of
 * the engine expects (see ::Buttons in input.hpp):
 *
 * - **GameCube** (always, on both consoles): the PAD library. The GC pad has no
 *   Select button, so the Z trigger stands in for it.
 * - **Wii** (TARGET_WII): the WPAD library, with the Wii Remote held sideways
 *   like an NES pad -- so the physical D-pad is rotated 90 degrees (pointing
 *   right is "up"), the 2 button is A and 1 is B, and +/- are Start/Select.
 *   Wii state is OR'd onto the matching GC port so either controller works.
 */
#include "internal.hpp"
#include <platform-nes/input.hpp>

#include <ogc/pad.h>
#ifdef TARGET_WII
#include <wiiuse/wpad.h>
#endif

void ogc_input_init() {
    PAD_Init();
#ifdef TARGET_WII
    WPAD_Init();
#endif
}

static u8 read_pad(const int chan) {
    const u32 held = PAD_ButtonsHeld(chan);
    u8 state = 0;
    if (held & PAD_BUTTON_A)     state |= A;
    if (held & PAD_BUTTON_B)     state |= B;
    if (held & PAD_TRIGGER_Z)    state |= SELECT;  // GC pad has no Select
    if (held & PAD_BUTTON_START) state |= START;
    if (held & PAD_BUTTON_UP)    state |= UP;
    if (held & PAD_BUTTON_DOWN)  state |= DOWN;
    if (held & PAD_BUTTON_LEFT)  state |= LEFT;
    if (held & PAD_BUTTON_RIGHT) state |= RIGHT;
    return state;
}

#ifdef TARGET_WII
static u8 read_wpad(const int chan) {
    const u32 held = WPAD_ButtonsHeld(chan);
    u8 state = 0;
    if (held & WPAD_BUTTON_2)     state |= A;
    if (held & WPAD_BUTTON_1)     state |= B;
    if (held & WPAD_BUTTON_MINUS) state |= SELECT;
    if (held & WPAD_BUTTON_PLUS)  state |= START;
    // Remote held sideways: the D-pad is rotated a quarter turn clockwise.
    if (held & WPAD_BUTTON_RIGHT) state |= UP;
    if (held & WPAD_BUTTON_LEFT)  state |= DOWN;
    if (held & WPAD_BUTTON_UP)    state |= LEFT;
    if (held & WPAD_BUTTON_DOWN)  state |= RIGHT;
    return state;
}
#endif

void PollControllers(u8* port1, u8* port2) {
    PAD_ScanPads();
    u8 p1 = read_pad(0);
    u8 p2 = read_pad(1);

#ifdef TARGET_WII
    WPAD_ScanPads();
    p1 |= read_wpad(WPAD_CHAN_0);
    p2 |= read_wpad(WPAD_CHAN_1);
#endif

    *port1 = p1;
    *port2 = p2;
}
