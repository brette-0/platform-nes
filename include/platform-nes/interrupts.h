#ifndef INTERRUPTS_H
#define INTERRUPTS_H
#include <stdint.h>
#include <stddef.h>

#ifndef TARGET_NES

typedef struct irq_t {
 uint8_t  id;
 uint16_t px;
 uint16_t py;
} irq_t;

typedef void (*irq_handler_fn)(void);

/* Resizable handler table — indexed by irq_t.id. Populated at startup
 * by constructors emitted by the IRQ(id) macro. */
extern irq_handler_fn* irqTable;
extern size_t          irqTableCount;
extern size_t          irqTableCap;

/* Pending-IRQ queue — drained each frame by the renderer. */
extern irq_t*  irqBuffer;
extern size_t  irqCount;
extern size_t  irqCap;

void RegisterIRQHandler(uint8_t id, irq_handler_fn fn);

#define IRQ(id) \
 static void irq ## id(void); \
 __attribute__((constructor)) \
 static void irq_register_ ## id(void) { \
  RegisterIRQHandler((id), irq ## id); \
 } \
 static void irq ## id(void)

#else

typedef uint8_t irq_t;

#define IRQ(id) \
 void irq ## id(void)

#endif

void SetNextIRQHandler(irq_t handle);

#endif
