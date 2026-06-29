/**
 * @file interrupts.hpp
 * @brief IRQ registration and dispatch.
 *
 * The NES target relies on the MMC3-style scanline IRQ; the desktop
 * target simulates the same semantics from the renderer. In both
 * cases the application registers handlers by numeric id with the
 * ::IRQ macro, then arms a specific handler for the next scanline
 * event via ::SetNextIRQHandler.
 *
 * On desktop builds handlers live in a dynamically-grown table keyed
 * by id, and the pending IRQ is held in ::irqPending for the renderer
 * to drain once per frame. On NES builds ::IRQ just declares a bare
 * `void irq<id>(void)` function that the crt0 dispatches directly.
 */
#ifndef INTERRUPTS_H
#define INTERRUPTS_H
#include <intsh>
using namespace br0::intsh;
#include <cstddef>

/** @brief Pixel coordinate used by ::ScheduleInterrupt (x, y in pixels). */
struct irq_pos_t { u16 x; u16 y; };

#ifndef TARGET_NES

/**
 * @brief A pending IRQ event queued for the renderer (desktop only).
 */
typedef struct irq_t {
 u8  id; /**< Handler id to dispatch when this scanline fires. */
 u16 px; /**< Pixel X coordinate at which the IRQ should fire. */
 u16 py; /**< Pixel Y coordinate at which the IRQ should fire. */
} irq_t;

/** @brief Signature of an IRQ handler (no arguments, no return value). */
typedef void (*irq_handler_fn)();

/**
 * @brief Dynamically-grown handler table, indexed by ::irq_t::id.
 *
 * Populated at startup by the constructors emitted by the ::IRQ macro.
 */
extern irq_handler_fn* irqTable;
/** @brief Number of handlers currently registered in ::irqTable. */
extern std::size_t          irqTableCount;
/** @brief Allocated capacity of ::irqTable. */
extern std::size_t          irqTableCap;

/**
 * @brief The single pending IRQ event for the renderer (desktop only).
 *
 * ::SetNextIRQHandler overwrites this slot; only one IRQ can be pending at
 * a time. The renderer fires it at the matching pixel coordinate and clears
 * ::irqPending.
 */
extern irq_t   irqPending;
/** @brief Non-zero when ::irqPending holds a valid event. */
extern bool    irqPendingValid;

/**
 * @brief Registers an IRQ handler under a numeric id.
 *
 * Normally invoked indirectly by the ::IRQ macro at program start;
 * application code rarely calls this directly.
 *
 * @param id Numeric identifier used later with ::SetNextIRQHandler.
 * @param fn Handler function to invoke when that id fires.
 */
void RegisterIRQHandler(u8 id, irq_handler_fn fn);

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
typedef u8 irq_t;

/**
 * @brief NES variant of ::IRQ — emits a top-level `irq<id>` interrupt handler.
 *
 * Tagged `interrupt_norecurse` so llvm-mos emits the full imaginary-register
 * save/restore prologue and epilogue, ending with RTI. This makes the function
 * safe to enter via `jmp` from a ::FAST_LOCKED_IRQ gate: the gate has already
 * restored A before jumping, so the handler's prologue sees the original
 * pre-interrupt register state and saves it correctly.
 *
 * @param id Integer handler id.
 */
#define IRQ(id) \
    ASM_LINKAGE __attribute__((used, interrupt_norecurse)) \
    void irq ## id(void)

#endif

/**
 * @brief Arms the handler that should fire on the next scanline IRQ.
 *
 * Overwrites any previously armed handler — this is a set, not an enqueue.
 * Only one handler can be pending at a time.
 *
 * @param handle Id of the previously registered handler (see ::IRQ).
 */
void SetNextIRQHandler(irq_t handle);

/**
 * @brief Returns the id of the currently armed IRQ handler.
 *
 * On NES this is the value last written by ::SetNextIRQHandler. On desktop
 * it is the id stored in the pending ::irq_t slot, or 0 if none is armed.
 */
irq_t GetCurrentIRQHandler();


#ifdef TARGET_NES

/**
 * @brief Declares the program's reset handler on NES builds.
 *
 * Expands to `int main()`, which the llvm-mos crt0 invokes at cold
 * boot. Follow the macro with the handler body.
 */
#define RESET int main()

/**
 * @brief Declares the NMI handler on NES builds.
 *
 * The resulting function is tagged `used` so the linker keeps it, and
 * `interrupt_norecurse` so the compiler emits a hardware-safe
 * prologue/epilogue. Follow with the handler body.
 */
#define NMI                                 \
ASM_LINKAGE __attribute__((used, interrupt_norecurse))  \
void nmi()

/**
 * @brief Active IRQ gate function pointer, dispatched from the hardware IRQ vector.
 *
 * Set via ::SetIRQ. Stored at an absolute address; the naked `irq()` dispatcher
 * jumps through it with `jmp (irq_fn)` — no compiler prologue, no register
 * save — so whatever the gate does first is what the CPU does first.
 */
extern "C" void (*irq_fn)();

/**
 * @brief Defines a fast-path IRQ gate with a constant-time denial path.
 *
 * Emits a naked, C-linkage function `gate_name` suitable for use with ::SetIRQ.
 * The gate is NOT compiled as an interrupt handler by llvm-mos (no imaginary
 * register save/restore), so its entire cost when the lock is clear is:
 *
 *   7 (hw entry) + 3 (pha) + 4 (lda abs) + 2 (bne not-taken) + 4 (pla) + 6 (rti)
 *   = 26 cycles, constant.  (ZP lock: 25 cycles.)
 *
 * If the lock is set the gate restores A and jumps directly into the target
 * handler (`irq<target>`), which IS `interrupt_norecurse` — its compiler-
 * generated prologue saves all imaginary registers from the pre-interrupt
 * state and its epilogue + RTI close the interrupt correctly.
 *
 * @param gate_name  C identifier for the gate function (e.g. `HUD_GATE`).
 * @param lock       Symbol name of an `atomic bool` (or `volatile bool`)
 *                   readable with a single `lda` — ideally `direct` (ZP, 3 cy)
 *                   or absolute (4 cy).  NOT a pointer; the value itself.
 * @param target     The `id` passed to ::IRQ that defines the success handler
 *                   (e.g. `HUD` expands the jump to `irqHUD`).
 */
#define FAST_LOCKED_IRQ(gate_name, lock, target)          \
    ASM_LINKAGE __attribute__((naked, used))              \
    void gate_name() {                                    \
        __asm__ (                                         \
            "pha\n\t"           /* 3 cy: save A        */ \
            "lda " #lock "\n\t" /* 3-4 cy: read lock   */ \
            "bne 1f\n\t"        /* 2 cy: not ready (not taken = faster) */ \
            "pla\n\t"           /* 4 cy: restore A     */ \
            "rti\n\t"           /* 6 cy: deny          */ \
            "1:\n\t"                                      \
            "pla\n\t"           /* 4 cy: restore A     */ \
            "jmp irq" #target   /* 3 cy: jump in       */ \
        );                                                \
    }

/**
 * @brief Arms a ::FAST_LOCKED_IRQ gate (or any naked IRQ function) as the
 *        target of the hardware IRQ vector for this frame.
 *
 * Stores the function pointer into ::irq_fn; the naked `irq()` dispatcher
 * (which lives at the hardware IRQ vector) jumps through it immediately,
 * with no intervening saves or overhead.
 *
 * @param gate  Function defined by ::FAST_LOCKED_IRQ (or another naked
 *              C-linkage IRQ function) to arm.
 */
#define SetIRQ(gate) (irq_fn = &(gate))

/**
 * @brief Schedule a cycle-counted IRQ on NES using silent DMC note chaining.
 *
 * Arms the already-set gate (::SetIRQ) to fire after @p cycles CPU cycles.
 * Intermediate DMC notes are chained until the budget is consumed; each note
 * fires the gate whose fast-path denial costs 26 cycles and whose ready path
 * jumps into the registered handler.  The optional @p ready flag is set to
 * true by the final note's completion so the gate can distinguish intermediate
 * chain pops from the real target scanline.
 *
 * @p location is unused on NES (the cycle count controls timing); it is
 * present for API parity with non-NES targets so call sites need no ifdefs.
 *
 * @param location  Target scanline position {x, y} in pixels (non-NES only).
 * @param cycles    CPU cycle budget from now until the IRQ should fire.
 * @param ready     Optional lock flag set true when the final note fires.
 *                  The gate tests this to decide deny vs. dispatch.
 */
void ScheduleInterrupt(irq_pos_t location, u16 cycles, volatile bool* ready = nullptr);

#else

#include "audio.hpp"

/**
 * @brief Library-side startup hook, called before user code on desktop builds.
 *
 * Invoked by the expansion of ::RESET. Initialises SDL subsystems,
 * opens the window, and prepares the audio mixer.
 */
extern void init();

/**
 * @brief Library-side teardown hook, called after user code returns.
 *
 * Invoked by the expansion of ::RESET. Closes the window, releases
 * audio devices, and shuts down SDL.
 */
extern void post();

/**
 * @brief Declares the program's reset handler on desktop builds.
 *
 * Expands to a `main` that calls ::init, runs the user's body, then
 * calls ::post. Follow the macro with the handler body — it becomes
 * the inline `usr_main` function.
 *
 * @code
 *   RESET {
 *     // one-shot setup, then the main loop
 *   }
 * @endcode
 */
#define RESET                       \
static void usr_main();         \
int main(){                     \
init();                     \
usr_main();                 \
post();                     \
}                               \
inline static void usr_main ()

/**
 * @brief Declares the NMI handler on desktop builds.
 *
 * The SDL3 renderer calls this once per simulated VBlank, matching
 * the NES NMI cadence so the same source works on both targets.
 */
#define NMI                     \
void nmi()

/**
 * @brief Handler id that ::ScheduleInterrupt will fire on non-NES targets.
 *
 * Set once at startup via ::SetIRQ.  ::ScheduleInterrupt combines this id
 * with the pixel position it receives to arm the correct entry in the emu
 * IRQ table (or the platform's own scanline-IRQ mechanism on GBA/NDS/etc.).
 */
extern u8 scheduledIRQId;

/**
 * @brief Registers the handler id that ::ScheduleInterrupt will fire.
 *
 * Non-NES equivalent of the NES ::SetIRQ(gate) macro.  Call once at startup
 * with the numeric id passed to ::IRQ for the split handler.
 *
 * On NES ::SetIRQ arms the naked gate function pointer; here it just stores
 * the id so ::ScheduleInterrupt can look it up in the emu IRQ table.
 *
 * @param id  Numeric id used in the corresponding ::IRQ(id) definition.
 */
#define SetIRQ(id) (scheduledIRQId = static_cast<u8>(id))

/**
 * @brief Schedule a position-based IRQ on non-NES targets.
 *
 * Arms the handler registered under ::scheduledIRQId (set by ::SetIRQ) to
 * fire when the renderer reaches pixel @p location.  On emu targets this
 * calls ::SetNextIRQHandler; on GBA/NDS the backend programs the hardware
 * HBlank counter to the requested scanline instead.
 *
 * @p cycles is unused on non-NES targets; it exists for API parity with the
 * NES implementation so call sites need no ifdefs.
 *
 * @param location  Target pixel coordinate {x, y} at which the IRQ fires.
 * @param cycles    CPU cycle budget (NES only; ignored here).
 * @param ready     Unused on non-NES (no gate lock needed); accepted for
 *                  API parity.
 */
void ScheduleInterrupt(irq_pos_t location, u16 cycles, volatile bool* ready = nullptr);

#endif

void reset();

#endif
