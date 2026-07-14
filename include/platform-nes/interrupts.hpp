/**
 * @file interrupts.hpp
 * @brief IRQ registration and dispatch.
 *
 * The NES target relies on the MMC3-style scanline IRQ; the desktop
 * target simulates the same semantics from the renderer. In both cases
 * the application defines a handler function directly (tagged with
 * ::interrupt on NES) and arms it by address with ::SetIRQ, then arms a
 * specific handler for the next scanline event via ::SetNextIRQHandler.
 *
 * On desktop builds the armed handler is a plain function pointer, and
 * the pending IRQ is held in ::irqPending for the renderer to drain once
 * per frame. On NES builds ::interrupt just tags a function so llvm-mos
 * emits the imaginary-register save/restore prologue and epilogue ending
 * in RTI.
 */
#ifndef INTERRUPTS_H
#define INTERRUPTS_H
#include <intsh>
using namespace br0::intsh;
#include <cstddef>

/**
 * @brief Tags a function as an interrupt entry point.
 *
 * On NES this expands to `ASM_LINKAGE __attribute__((used, interrupt_norecurse))
 * void`: `ASM_LINKAGE` (extern "C") keeps the symbol name stable so it can be
 * jumped to by raw name (e.g. from a ::FAST_LOCKED_IRQ gate), `used` stops LTO
 * from discarding a function nothing calls via ordinary C++ call syntax (it's
 * only ever reached via `jmp`/a stored function pointer), and
 * `interrupt_norecurse` makes llvm-mos emit the full imaginary-register
 * save/restore prologue and epilogue, ending with RTI. Off NES it's just
 * `void` — there's no hardware vector or register file to protect.
 *
 * Use it in place of a return type:
 *
 * @code
 *   interrupt HUD_IRQ() {
 *     // runs when armed via SetIRQ(HUD_IRQ) and the scanline fires
 *   }
 * @endcode
 */
#ifdef TARGET_NES
#define interrupt ASM_LINKAGE __attribute__((used, interrupt_norecurse)) void
#else
#define interrupt void
#endif

/** @brief Pixel coordinate used by ::ScheduleInterrupt (x, y in pixels). */
struct irq_pos_t { u16 x; u16 y; };

#ifndef TARGET_NES

/** @brief Signature of an IRQ handler (no arguments, no return value). */
typedef void (*irq_handler_fn)();

/**
 * @brief A pending IRQ event queued for the renderer (desktop only).
 */
typedef struct irq_t {
 irq_handler_fn fn; /**< Handler to invoke when this scanline fires. */
 u16 px; /**< Pixel X coordinate at which the IRQ should fire. */
 u16 py; /**< Pixel Y coordinate at which the IRQ should fire. */
} irq_t;

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

#else

/** @brief Opaque IRQ handle on NES builds. */
typedef u8 irq_t;

#endif

/**
 * @brief Arms the handler that should fire on the next scanline IRQ.
 *
 * Overwrites any previously armed handler — this is a set, not an enqueue.
 * Only one handler can be pending at a time.
 *
 * @param handle Previously armed handle.
 */
void SetNextIRQHandler(irq_t handle);

/**
 * @brief Returns the currently armed IRQ handler.
 *
 * On NES this is the value last written by ::SetNextIRQHandler. On desktop
 * it is the pending ::irq_t slot (its handler is null if none is armed).
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
 * @brief Deny-path target for ::FAST_LOCKED_IRQ_CHAINED: advances the DMC
 *        chain ::ScheduleInterrupt uses to build up a multi-note delay.
 *
 * @note A single-byte DMC note's IRQ fires almost immediately after arming
 * (bytes-remaining hits zero at DMA fetch time, not after the byte's
 * rate-gated playback), independent of the rate register — so one note
 * alone can't produce a useful, tunable delay. This re-arms a fresh
 * one-byte note and counts down a caller-set budget (::ScheduleInterrupt's
 * `steps`); once the count reaches zero it sets ::dmcChainLock so the next
 * fire dispatches to the real handler instead of denying again.
 */
extern "C" void dmc_chain_handler();

/**
 * @brief Chain lock read by a ::FAST_LOCKED_IRQ_CHAINED gate armed via
 *        ::ScheduleInterrupt.
 *
 * Clear while ::dmc_chain_handler is still re-arming notes; set once the
 * requested chain length is reached, so the gate dispatches to its target
 * on the next DMC IRQ. Pass this symbol as a ::FAST_LOCKED_IRQ_CHAINED
 * gate's `lock` argument.
 */
extern "C" volatile bool dmcChainLock;

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
 * handler, which IS `interrupt_norecurse` — its compiler-generated prologue
 * saves all imaginary registers from the pre-interrupt state and its
 * epilogue + RTI close the interrupt correctly.
 *
 * Use this variant when there are no intermediate DMC notes to advance on
 * denial (single-note schedules, or when the chain is managed externally).
 * For DMC chaining use ::FAST_LOCKED_IRQ_CHAINED instead.
 *
 * @param gate_name  C identifier for the gate function (e.g. `HUD_GATE`).
 * @param lock       Symbol name of an `atomic bool` (or `volatile bool`)
 *                   readable with a single `lda` — ideally `direct` (ZP, 3 cy)
 *                   or absolute (4 cy).  NOT a pointer; the value itself.
 * @param target     C identifier of the success handler (e.g. a function
 *                   defined with ::interrupt). Must be `ASM_LINKAGE` (extern
 *                   "C") so this can jump to it by raw symbol name.
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
            "jmp " #target      /* 3 cy: jump in       */ \
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
 * Deny path (intermediate):  26 cy (gate) + on_deny's own cost.
 * Deny path (final note):     sets lock=true so the NEXT fire dispatches.
 * Dispatch path:              identical to ::FAST_LOCKED_IRQ.
 *
 * @param gate_name  C identifier for the gate function.
 * @param lock       Symbol of the `volatile bool` gate lock (true = dispatch).
 * @param target     C identifier of the handler that fires on dispatch (see
 *                   ::FAST_LOCKED_IRQ).
 * @param on_deny    C symbol to jump to on denial; must be ::dmc_chain_handler
 *                   or a compatible function safe to enter via `jmp` mid-
 *                   interrupt (naked and self-managing, or `interrupt_norecurse`
 *                   like ::dmc_chain_handler) that saves/restores A and RTIs.
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
            "jmp " #target         /* 3 cy: jump in       */ \
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
 * @brief Schedule a chained-DMC-note IRQ on NES.
 *
 * @note A single one-byte DMC note's IRQ fires almost immediately after
 * arming — bytes-remaining hits zero at DMA fetch time, not after the
 * byte's rate-gated playback — so the rate register can't move it, and the
 * length register can't express anything between 1 byte and 17 (16*L+1),
 * whose minimum delay (7344+ cycles) is too coarse for a few-thousand-cycle
 * budget. So @p steps is not a rate index: it's how many one-byte notes to
 * chain back to back via ::FAST_LOCKED_IRQ_CHAINED / ::dmc_chain_handler
 * before dispatching to the real handler. Each link costs roughly the same
 * small, DMA-dominated ~280-320 cycles plus the chain handler's own re-arm
 * overhead, so total delay is approximately linear in @p steps but not an
 * exact hardware constant — calibrate empirically against the target (see
 * the demo's HUD-split IRQ, which exposes a spin-wait iteration count for
 * exactly this). The call site must arm ::SetIRQ with a
 * ::FAST_LOCKED_IRQ_CHAINED gate (lock = ::dmcChainLock, on_deny =
 * ::dmc_chain_handler), not the plain target handler directly — see the
 * demo's HUD-split IRQ for a worked example.
 *
 * @p location is unused on NES (the step count controls timing); it is
 * present for API parity with non-NES targets so call sites need no ifdefs.
 *
 * @param location  Target scanline position {x, y} in pixels (non-NES only).
 * @param steps     Number of one-byte DMC notes to chain before dispatching
 *                  to the real handler (0 = dispatch on the very first
 *                  note, no chaining).
 * @param ready     Optional flag set true once the note is armed; purely
 *                  informational now that dispatch has no gate to test it.
 */
void ScheduleInterrupt(irq_pos_t location, u8 steps, volatile bool* ready = nullptr);

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
 * @param target     Handler function to dispatch to.
 */
#define FAST_LOCKED_IRQ(gate_name, lock, target) \
    static constexpr irq_handler_fn gate_name = (target)

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
 * @param target     Handler function to dispatch to.
 * @param on_deny    Unused on non-NES; accepted for API parity with NES.
 */
#define FAST_LOCKED_IRQ_CHAINED(gate_name, lock, target, on_deny) \
    static constexpr irq_handler_fn gate_name = (target)

/**
 * @brief Handler ::ScheduleInterrupt will fire on non-NES targets.
 *
 * Set once at startup via ::SetIRQ.  ::ScheduleInterrupt combines this
 * pointer with the pixel position it receives to arm the pending IRQ event
 * (or the platform's own scanline-IRQ mechanism on GBA/NDS/etc.).
 */
extern irq_handler_fn scheduledIRQHandler;

/**
 * @brief Registers the handler that ::ScheduleInterrupt will fire.
 *
 * Non-NES equivalent of the NES ::SetIRQ(gate) macro.  Call once at startup
 * with the handler function for the split.
 *
 * @param fn  Handler function to arm.
 */
#define SetIRQ(fn) (scheduledIRQHandler = (fn))

/**
 * @brief Schedule a position-based IRQ on non-NES targets.
 *
 * @note Written for API parity with the NES implementation, which drives
 * timing from a single rudimentary DMC note (see the NES overload's docs)
 * rather than this function's pixel-position dispatch.
 *
 * Arms the handler set by ::SetIRQ to fire when the renderer reaches pixel
 * @p location.  On emu targets this calls ::SetNextIRQHandler; on GBA/NDS
 * the backend programs the hardware HBlank counter to the requested
 * scanline instead.
 *
 * @p steps is unused on non-NES targets; it exists for API parity with the
 * NES implementation so call sites need no ifdefs.
 *
 * @param location  Target pixel coordinate {x, y} at which the IRQ fires.
 * @param steps     DMC chain length (NES only; ignored here).
 * @param ready     Unused on non-NES; accepted for API parity.
 */
void ScheduleInterrupt(irq_pos_t location, u8 steps, volatile bool* ready = nullptr);

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
