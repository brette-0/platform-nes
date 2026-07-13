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

#ifdef NES_MAPPER_BANKSWITCHED
/**
 * @brief Declares the program's reset handler on bankswitched NES builds.
 *
 * Expands to `int main()`, which the llvm-mos crt0 invokes at cold boot,
 * pinned to `.prg_rom_fixed`. main is the one piece of code that must
 * always be resident no matter what any switchable window currently holds
 * -- everything else, including the bankswitching calls themselves, runs
 * from it. NES_MAPPER_BANKSWITCHED is set by CMakeLists.txt from
 * LLVM_MOS_PLATFORM, so this follows the mapper choice automatically.
 * Follow the macro with the handler body.
 */
#define RESET __attribute__((section(".prg_rom_fixed"))) int main()
#else
/**
 * @brief Declares the program's reset handler on NES builds.
 *
 * Expands to `int main()`, which the llvm-mos crt0 invokes at cold
 * boot. Follow the macro with the handler body. NROM has no banks to
 * escape to, so unlike the bankswitched variant of this macro, main isn't
 * pinned to any particular section.
 */
#define RESET int main()
#endif

inline void EnableInterrupts()  { __asm__ volatile ("cli"); }
inline void DisableInterrupts() { __asm__ volatile ("sei"); }

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
 * Set via ::SetIRQ. Stored in absolute BSS; the naked `irq()` dispatcher
 * jumps through it with `jmp (irqTrampoline)` — no compiler prologue, no register
 * save — so whatever the gate does first is what the CPU does first.
 */
extern "C" void (*irqTrampoline)();

/**
 * @brief Naked DMC chain advance handler — the deny-path trampoline for
 *        ::FAST_LOCKED_IRQ_CHAINED.
 *
 * Called directly (via `jmp`) from the gate's deny path.  Arms the next
 * queued DMC note, advances the chain index, and on the final note sets
 * the gate's lock flag so the next IRQ dispatches instead of denying.
 * Does NOT touch irqTrampoline — the gate stays armed at all times.
 */
extern "C" void dmc_chain_handler();

/**
 * @brief Defines a fast-path IRQ gate with a no-op deny path.
 *
 * Emits a naked, C-linkage function `gate_name` suitable for use with ::SetIRQ.
 * The gate is NOT compiled as an interrupt handler by llvm-mos (no imaginary
 * register save/restore), so its entire cost when the lock is clear (deny
 * path: `pla; rti`) is:
 *
 *   7 (hw entry) + 3 (pha) + 4 (lda abs) + 2 (bne not-taken) + 4 (pla) + 6 (rti)
 *   = 26 cycles, constant.  (ZP lock: 25 cycles.)
 *
 * If the lock is set the gate restores A and jumps directly into the target
 * handler (`irq<target>`), which IS `interrupt_norecurse` — its compiler-
 * generated prologue saves all imaginary registers from the pre-interrupt
 * state and its epilogue + RTI close the interrupt correctly.
 *
 * Use this variant when there are no intermediate DMC notes to advance on
 * denial (single-note schedules, or when the chain is managed externally).
 * For DMC chaining use ::FAST_LOCKED_IRQ_CHAINED instead.
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
            "lda $4015\n\t"     /* 4 cy: ack DMC IRQ   */ \
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
 * @brief Defines a fast-path IRQ gate whose deny path advances a DMC chain.
 *
 * Same dispatch path as ::FAST_LOCKED_IRQ.  On denial (lock not set), instead
 * of `rti`, restores A and jumps to ::dmc_chain_handler, which arms the next
 * note, advances the chain index, and — on the final note — sets the lock so
 * the next fire dispatches.  irqTrampoline is NEVER redirected away from the
 * gate; all intermediate and final DMC IRQs go through the same deny/dispatch
 * test.
 *
 * Deny path (intermediate):  26 cy (gate) + ~63 cy (chain handler) = ~89 cy.
 * Deny path (final note):     sets lock=true so the NEXT fire dispatches.
 * Dispatch path:              identical to ::FAST_LOCKED_IRQ.
 *
 * @param gate_name  C identifier for the gate function.
 * @param lock       Symbol of the `volatile bool` gate lock (true = dispatch).
 * @param target     ::IRQ id whose handler fires on dispatch.
 * @param on_deny    C symbol to jump to on denial; must be ::dmc_chain_handler
 *                   or a compatible naked function that saves/restores A and RTIs.
 */
#define FAST_LOCKED_IRQ_CHAINED(gate_name, lock, target, on_deny) \
    ASM_LINKAGE __attribute__((naked, used))              \
    void gate_name() {                                    \
        __asm__ (                                         \
            "pha\n\t"              /* 3 cy: save A        */ \
            "lda $4015\n\t"        /* 4 cy: ack DMC IRQ   */ \
            "lda " #lock "\n\t"    /* 3-4 cy: read lock   */ \
            "bne 1f\n\t"           /* 2 cy: not ready     */ \
            "pla\n\t"              /* 4 cy: restore A     */ \
            "jmp " #on_deny "\n\t" /* 3 cy: advance chain */ \
            "1:\n\t"                                         \
            "pla\n\t"              /* 4 cy: restore A     */ \
            "jmp irq" #target      /* 3 cy: jump in       */ \
        );                                                \
    }

/**
 * @brief Arms a ::FAST_LOCKED_IRQ gate (or any naked IRQ function) as the
 *        target of the hardware IRQ vector for this frame.
 *
 * Stores the function pointer into ::irqTrampoline; the naked `irq()` dispatcher
 * (which lives at the hardware IRQ vector) jumps through it immediately,
 * with no intervening saves or overhead.
 *
 * @param gate  Function defined by ::FAST_LOCKED_IRQ (or another naked
 *              C-linkage IRQ function) to arm.
 */
#define SetIRQ(gate) (irqTrampoline = &(gate))

/**
 * @brief Schedule a cycle-counted IRQ on NES using silent DMC note chaining.
 *
 * @warning Currently a complete no-op stub on NES (see `src/nes/interrupts.cpp`).
 * DMC note chaining is prototyped here and in ::FAST_LOCKED_IRQ_CHAINED but not
 * yet implemented -- calling this does nothing on real NES hardware today.
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
 * @brief Enables the CPU interrupt line — a no-op off NES.
 *
 * @note This function does nothing on non-NES targets: the emu/console
 * backends dispatch their scanline IRQ synchronously from the renderer, with
 * no hardware interrupt line to mask in the first place. Declared so a
 * ::RESET body written once (calling ::EnableInterrupts / ::DisableInterrupts
 * around setup) compiles unchanged on every target.
 */
inline void EnableInterrupts()  {}

/**
 * @brief Disables the CPU interrupt line — a no-op off NES.
 *
 * @note This function does nothing on non-NES targets; see ::EnableInterrupts.
 */
inline void DisableInterrupts() {}

/**
 * @brief Non-NES declaration of ::FAST_LOCKED_IRQ's DMC-chain deny-path
 *        target.
 *
 * @note This function does nothing on non-NES targets: there is no DMC
 * hardware or cycle-counted IRQ chain to advance off NES. Declared so
 * ::FAST_LOCKED_IRQ_CHAINED's @p on_deny argument (e.g. a call site passing
 * `dmc_chain_handler`) resolves the same symbol on every target.
 */
extern "C" void dmc_chain_handler();

/**
 * @brief Non-NES equivalent of ::FAST_LOCKED_IRQ.
 *
 * There is no hardware interrupt line to gate off NES, so this collapses
 * @p gate_name to a compile-time alias for @p target: `SetIRQ(gate_name)`
 * then arms the same handler the NES gate would have jumped to directly.
 *
 * @param gate_name  Identifier usable with ::SetIRQ, same as on NES.
 * @param lock       Unused on non-NES; accepted for API parity with NES.
 * @param target     ::IRQ id to dispatch to.
 */
#define FAST_LOCKED_IRQ(gate_name, lock, target) \
    static constexpr u8 gate_name = (target)

/**
 * @brief Non-NES equivalent of ::FAST_LOCKED_IRQ_CHAINED.
 *
 * Same non-NES behavior as ::FAST_LOCKED_IRQ: @p gate_name collapses to a
 * compile-time alias for @p target. @p lock and @p on_deny are unused — there
 * is no DMC chain to advance off NES (see ::dmc_chain_handler) — but are
 * still accepted so a call site written once compiles on every target.
 *
 * @param gate_name  Identifier usable with ::SetIRQ, same as on NES.
 * @param lock       Unused on non-NES; accepted for API parity with NES.
 * @param target     ::IRQ id to dispatch to.
 * @param on_deny    Unused on non-NES; accepted for API parity with NES.
 */
#define FAST_LOCKED_IRQ_CHAINED(gate_name, lock, target, on_deny) \
    static constexpr u8 gate_name = (target)

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
 * @warning DMC-driven cycle scheduling is prototype-stage: the NES
 * implementation of this function is currently a complete no-op stub, so
 * nothing calls back through it there yet. This non-NES implementation does
 * dispatch (by pixel position, as below), but it was written for API parity
 * with that still-unfinished NES path -- treat the whole function as
 * unstable until the NES side lands.
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

/**
 * @brief Soft-resets the application.
 *
 * On NES builds, re-enters through the hardware reset vector (`jmp ($FFFC)`),
 * the same entry point a cold boot uses. On desktop builds, runs ::post to
 * tear down SDL and audio, then calls `exit(0)`. Lets application code spin
 * back to ::RESET on NES or quit cleanly on desktop from the same call site,
 * with no `#ifdef` needed.
 */
void reset();

#endif
