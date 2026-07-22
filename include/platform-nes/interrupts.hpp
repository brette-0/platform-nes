/**
 * @file interrupts.hpp
 * @brief IRQ registration and dispatch.
 *
 * The NES target relies on the MMC3-style scanline IRQ; the desktop
 * target simulates the same semantics from the renderer. On NES the
 * application defines the single hardware IRQ handler directly with
 * ::IRQ, placed at the hardware vector exactly like ::NMI -- there is no
 * runtime arming. On desktop, which has no hardware vector to place code
 * at, the renderer instead schedules a scanline event with
 * ::SetNextIRQHandler and fires it by calling the given function pointer.
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
 * void`: `ASM_LINKAGE` (extern "C") keeps the symbol name stable so the linker
 * script (nes.ld) can place it directly at the hardware vector by raw symbol
 * name (`nmi`/`irq`), `used` stops LTO from discarding a function nothing
 * calls via ordinary C++ call syntax -- that raw-symbol reference from the
 * vector table is invisible to the compiler's call-graph analysis -- and
 * `interrupt_norecurse` makes llvm-mos emit the full imaginary-register
 * save/restore prologue and epilogue, ending with RTI. Off NES it's just
 * `void` — there's no hardware vector or register file to protect.
 *
 * Use it in place of a return type. ::NMI and ::IRQ expand to this same
 * attribute set, pinned to the two hardware vector symbol names:
 *
 * @code
 *   interrupt MyHandler() {
 *     // full imaginary-register save/restore, ending in RTI
 *   }
 * @endcode
 */
#ifdef TARGET_NES
#define interrupt ASM_LINKAGE __attribute__((used, interrupt_norecurse)) void
#else
#define interrupt void
#endif

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
 * prologue/epilogue. Follow with the handler body. The function is placed
 * directly at the hardware NMI vector (mos-platform's nes.ld: `.vector
 * 0xfffa : { SHORT(nmi) ... }`) by raw symbol name -- there is no
 * indirection and no runtime rearming; NMI has exactly one handler, chosen
 * at compile time.
 *
 * Takes an optional attribute-specifier-seq, spliced in right after the
 * `extern "C"` this expands to -- the same position real-world code uses
 * for e.g. `extern "C" [[noreturn]] void abort();`. That position (not a
 * leading prefix on the macro invocation) is required: `extern "C" void
 * nmi()` is a *linkage-specification* wrapping a declaration, not an
 * ordinary declaration itself, and a leading attribute-specifier-seq is only
 * grammatically valid at the start of an ordinary declaration --
 * `[[noreturn]] extern "C" void nmi();` is not valid C++. Write:
 *
 * @code
 *   NMI() {
 *     ...   // ordinary handler: falls through, compiler-generated RTI returns
 *   }
 *
 *   NMI([[noreturn]]) {
 *     for (;;) { ... }   // body genuinely never falls off the end
 *   }
 * @endcode
 *
 * `[[noreturn]]` is only correct if the body truly never reaches its closing
 * brace by falling through (an infinite loop, or a tail transfer via ::JUMP
 * into something else that itself never returns) -- if it does return
 * normally every call, the compiler-generated RTI is exactly what hands
 * control back to the interrupted code, and `[[noreturn]]` is a lie about
 * that.
 *
 * @note Unverified: whether llvm-mos's `interrupt_norecurse` lowering
 * actually drops the RTI epilogue for a `[[noreturn]]`-marked, genuinely
 * non-falling-through body, or unconditionally emits it regardless (RTI
 * insertion may be tied to the interrupt calling convention itself, not to
 * the generic noreturn/unreachable codegen path that ordinary functions
 * use). If you need a vector-placed function with *guaranteed* zero
 * compiler-generated epilogue, don't rely on `[[noreturn]]` here -- use
 * ::NAKED_NMI / ::NAKED_IRQ instead, which sidestep the question entirely by
 * never asking llvm-mos to generate interrupt bookkeeping in the first
 * place.
 */
#define NMI(...)                                       \
ASM_LINKAGE __VA_ARGS__ __attribute__((used, interrupt_norecurse))  \
void nmi()

/**
 * @brief Declares the IRQ handler on NES builds.
 *
 * Same shape as ::NMI: the resulting function is placed directly at the
 * hardware IRQ vector (nes.ld: `SHORT(irq)`) by raw symbol name, tagged
 * `used`/`interrupt_norecurse`. There is exactly one IRQ handler, chosen at
 * compile time -- no gate, no lock, no runtime arming. Whatever IRQ sources
 * are enabled (e.g. the MMC3 scanline counter) all vector here; the handler
 * itself is responsible for telling them apart if more than one is active.
 * Follow with the handler body: `IRQ()` for an ordinary handler,
 * `IRQ([[noreturn]])` composes the same way as on ::NMI, with the same
 * caveat -- only correct if the body never falls through.
 */
#define IRQ(...)                                       \
ASM_LINKAGE __VA_ARGS__ __attribute__((used, interrupt_norecurse))  \
void irq()

/**
 * @brief Declares the NMI handler on NES builds with zero compiler-generated
 *        interrupt bookkeeping.
 *
 * Same vector placement as ::NMI (raw symbol name `nmi`, nes.ld: `SHORT(nmi)`
 * at $fffa), but tagged `naked` instead of `interrupt_norecurse`: llvm-mos
 * emits NO prologue or epilogue at all for this function -- no
 * imaginary-register save/restore, no compiler-generated RTI, nothing.
 * Whatever asm the body contains is *exactly* what ends up at the vector,
 * verbatim. The tradeoff for that certainty is that `naked` function bodies
 * may contain only `asm` statements -- ::JUMP / ::JUMP_INDIRECT are plain
 * single `asm` statements with no wrapper, so they're fine here, but nothing
 * else (no C++ statements at all) is, and the body is entirely responsible
 * for its own interrupt housekeeping if it doesn't tail-jump straight into
 * something else that handles that itself.
 *
 * The canonical use is a bare tail-jump trampoline into a real, separately
 * defined ::interrupt handler -- that handler's own prologue/epilogue does
 * all the actual register save/restore and RTI, so the vector-placed
 * function needs to do nothing but jump:
 *
 * @code
 *   interrupt nmiHandler() { ... }   // real logic, own save/restore + RTI
 *
 *   NAKED_NMI {
 *     JUMP(nmiHandler);
 *   }
 * @endcode
 *
 * Prefer plain ::NMI unless you specifically need the guarantee that no
 * compiler-generated code exists at the vector at all -- see the `@note` on
 * ::NMI's `[[noreturn]]` for why that guarantee isn't otherwise available.
 */
#define NAKED_NMI                            \
ASM_LINKAGE __attribute__((naked, used))     \
void nmi()

/**
 * @brief Declares the IRQ handler on NES builds with zero compiler-generated
 *        interrupt bookkeeping.
 *
 * Same relationship to ::IRQ as ::NAKED_NMI has to ::NMI: `naked` instead of
 * `interrupt_norecurse`, raw symbol name `irq` (nes.ld: `SHORT(irq)` at
 * $fffe), body must be pure `asm` (::JUMP / ::JUMP_INDIRECT are fine; nothing
 * else is). Same canonical use: a bare tail-jump into a separately defined
 * ::interrupt handler that does the real save/restore and RTI itself.
 */
#define NAKED_IRQ                            \
ASM_LINKAGE __attribute__((naked, used))     \
void irq()

/**
 * @brief Direct unconditional jump to `target`, a function (a fixed,
 *        link-time-known entry point, e.g. another ::interrupt-tagged
 *        handler). Never falls through.
 *
 * Nothing more than `__asm__ volatile ("jmp " #target)` -- a single inline
 * `asm` statement, no wrapper, no runtime or compile-time branch. That
 * means it's usable anywhere plain `asm` is, including inside a `naked`
 * function body (which may contain only `asm` statements):
 *
 * @code
 *   interrupt nmiHandler() { ... }   // real logic, own save/restore + RTI
 *
 *   NAKED_NMI {
 *     JUMP(nmiHandler);
 *   }
 * @endcode
 */
#define JUMP(target) __asm__ volatile ("jmp " #target)

/**
 * @brief Indirect unconditional jump through `target`, a `void(*)()`-typed
 *        variable holding a runtime-chosen entry point (e.g. ::irqTrampoline
 *        elsewhere). Never falls through.
 *
 * Nothing more than `__asm__ volatile ("jmp (" #target ")")` -- same
 * single-statement, zero-overhead shape as ::JUMP, just through the
 * variable's contents instead of straight to a fixed address. Also usable
 * inside a `naked` function body.
 */
#define JUMP_INDIRECT(target) __asm__ volatile ("jmp (" #target ")")

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
 * @brief Declares the IRQ handler on non-NES builds.
 *
 * Non-NES equivalent of ::NMI, for symmetry with NES source that defines
 * both vectors; not currently driven by any renderer backend.
 */
#define IRQ                     \
void irq()

/**
 * @brief Non-NES equivalent of ::NAKED_NMI.
 *
 * There is no hardware vector or naked-function restriction off NES --
 * ::nmi() is just an ordinary function the renderer calls once per
 * simulated VBlank -- so this collapses to the same thing as ::NMI. Exists
 * so shared source (e.g. a trampoline written once as `NAKED_NMI { JUMP(x); }`)
 * compiles unchanged on every target.
 */
#define NAKED_NMI               \
void nmi()

/**
 * @brief Non-NES equivalent of ::NAKED_IRQ. See ::NAKED_NMI.
 */
#define NAKED_IRQ               \
void irq()

/**
 * @brief Non-NES equivalent of ::JUMP: an ordinary tail call.
 *
 * There's no `naked` body restriction to a single `asm` statement off NES,
 * so this is just `target();` -- a normal call, immediately followed by
 * the compiler-generated return from ::NAKED_NMI / ::NAKED_IRQ.
 */
#define JUMP(target) target()

/**
 * @brief Non-NES equivalent of ::JUMP_INDIRECT: an ordinary call through
 *        a function-pointer variable.
 */
#define JUMP_INDIRECT(target) (target)()

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
