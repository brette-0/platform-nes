#ifndef INTERRUPTS_H
#define INTERRUPTS_H
#include <stdint.h>

#define IRQ(id) \
 void irq ## id(void)

#ifndef TARGET_NES


typedef struct irq_t {
 uint8_t  type;
 uint16_t px;
 uint16_t py;
} irq_t;

#else
typedef uint8_t irq_t;

#endif

void SetNextIRQHandler(irq_t handle);

#endif