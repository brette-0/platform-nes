/**
 * @file interrupts.h
 * @brief IRQ registration and dispatch.
 *
 * The NES target relies on the MMC3-style scanline IRQ; the desktop
 * target simulates the same semantics from the renderer. In both
 * cases the application registers handlers by numeric id with the
 * ::IRQ macro, then arms a specific handler for the next scanline
 * event via ::SetNextIRQHandler.
 *
 * On desktop builds handlers live in a dynamically-grown table keyed
 * by id, and pending IRQs are queued in ::irqBuffer for the renderer
 * to drain once per frame. On NES builds ::IRQ just declares a bare
 * `void irq<id>(void)` function that the crt0 dispatches directly.
 */
#ifndef INTERRUPTS_H
#define INTERRUPTS_H
#include <stdint.h>
#include <stddef.h>

#ifndef TARGET_NES

/**
 * @brief A pending IRQ event queued for the renderer (desktop only).
 */
typedef struct irq_t {
 uint8_t  id; /**< Handler id to dispatch when this scanline fires. */
 uint16_t px; /**< Pixel X coordinate at which the IRQ should fire. */
 uint16_t py; /**< Pixel Y coordinate at which the IRQ should fire. */
} irq_t;

/** @brief Signature of an IRQ handler (no arguments, no return value). */
typedef void (*irq_handler_fn)(void);

/**
 * @brief Dynamically-grown handler table, indexed by ::irq_t::id.
 *
 * Populated at startup by the constructors emitted by the ::IRQ macro.
 */
extern irq_handler_fn* irqTable;
/** @brief Number of handlers currently registered in ::irqTable. */
extern size_t          irqTableCount;
/** @brief Allocated capacity of ::irqTable. */
extern size_t          irqTableCap;

/** @brief FIFO of pending IRQs drained once per frame by the renderer. */
extern irq_t*  irqBuffer;
/** @brief Number of entries currently in ::irqBuffer. */
extern size_t  irqCount;
/** @brief Allocated capacity of ::irqBuffer. */
extern size_t  irqCap;

/**
 * @brief Registers an IRQ handler under a numeric id.
 *
 * Normally invoked indirectly by the ::IRQ macro at program start;
 * application code rarely calls this directly.
 *
 * @param id Numeric identifier used later with ::SetNextIRQHandler.
 * @param fn Handler function to invoke when that id fires.
 */
void RegisterIRQHandler(uint8_t id, irq_handler_fn fn);

/**
 * @brief Declares an IRQ handler with the given id.
 *
 * The macro expands to a forward declaration, a constructor that
 * registers the handler under @p id at program start, and the opening
 * of the handler body. Use it as a function definition:
 *
 * @code
 *   IRQ(3) {
 *     // runs when SetNextIRQHandler(3) is armed and the scanline fires
 *   }
 * @endcode
 *
 * @param id Integer handler id, unique within the program.
 */
#define IRQ(id) \
 static void irq ## id(void); \
 __attribute__((constructor)) \
 static void irq_register_ ## id(void) { \
  RegisterIRQHandler((id), irq ## id); \
 } \
 static void irq ## id(void)

#else

/** @brief Opaque IRQ handle on NES builds; the id is used directly. */
typedef uint8_t irq_t;

/**
 * @brief NES variant of ::IRQ — emits a top-level `irq<id>` function.
 * @param id Integer handler id.
 */
#define IRQ(id) \
 void irq ## id(void)

#endif

/**
 * @brief Arms the handler that should fire on the next scanline IRQ.
 * @param handle Id of the previously registered handler (see ::IRQ).
 */
void SetNextIRQHandler(irq_t handle);

#endif
