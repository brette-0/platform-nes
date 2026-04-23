/**
 * @file reset.h
 * @brief Program entry-point and NMI-handler macros.
 *
 * Exposes ::RESET and ::NMI — the two macros the application uses to
 * declare its reset routine and VBlank interrupt handler — with
 * backend-specific expansions selected by ::TARGET_NES:
 *
 * - On NES, ::RESET maps directly to `int main()` and ::NMI maps to a
 *   `void nmi()` tagged for the llvm-mos crt0.
 * - On desktop (SDL3), ::RESET wraps the user's body between
 *   library-side ::init and ::post calls so SDL / audio / video come
 *   up before user code and tear down after it. ::NMI becomes a
 *   regular `void nmi()` that the SDL3 renderer calls once per
 *   simulated VBlank.
 */
#ifndef RESET_H
#define RESET_H

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
__attribute__((used, interrupt_norecurse))  \
void nmi()

#else

#include "audio.h"

/**
 * @brief Library-side startup hook, called before user code on desktop builds.
 *
 * Invoked by the expansion of ::RESET. Initialises SDL subsystems,
 * opens the window, and prepares the audio mixer.
 */
extern void init(void);

/**
 * @brief Library-side teardown hook, called after user code returns.
 *
 * Invoked by the expansion of ::RESET. Closes the window, releases
 * audio devices, and shuts down SDL.
 */
extern void post(void);

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

#endif

#endif //RESET_H
