#ifndef HANDLERS_h
#define HANDLERS_h

#include <platform-nes/interrupts.h>

enum {
    SPRITE_ZERO
};

#ifdef TARGET_NES
#define IRQ_SPRITE_ZERO 0
#else
#define IRQ_SPRITE_ZERO \
    (irq_t){.id = 0, .px = 0, .py = 16}
#endif

#endif