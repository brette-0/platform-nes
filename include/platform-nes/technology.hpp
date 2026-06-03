/**
 * @file technology.hpp
 * @brief Low-level memory access primitives, toolchain shims, and
 *        assembly-backed data helpers.
 *
 * This header groups the "platform plumbing" used by the rest of the
 * library:
 *
 * - ::PEEK / ::POKE / ::SPACESHIP — portable memory and compare ops.
 * - ::atomic — maps to `volatile` on NES and `_Atomic` elsewhere.
 * - ::MINSIZE — Clang-only size-optimisation hint (silently dropped
 *   under GCC).
 * - ::CHARMAP, ::MAPPED_STRING, ::NULL_TERMINATED_MAPPED_STRING — define
 *   per-project character maps and emit translated strings into
 *   `.rodata` from inline assembly.
 * - ::PopulateFromBuffer / ::PopulateFromProvider — generic strided
 *   byte copies used by the video and audio subsystems.
 */
#ifndef TECHNOLOGY_H
#define TECHNOLOGY_H

#include <intsh>
using namespace br0::intsh;
#include <type_traits>

/**
 * @brief Reads one byte from the given address, treating it as `volatile const`.
 * @param addr Pointer or integer address to dereference.
 * @return The byte stored at @p addr.
 */
#define PEEK(addr) (*(volatile const unsigned char *)(addr))

/**
 * @brief Writes one byte to the given address via a `volatile` store.
 * @param addr Pointer or integer address to write to.
 * @param data Byte value to store.
 */
#define POKE(addr, data) (*(volatile unsigned char *)(addr)) = data

/**
 * @brief Three-way comparison.
 *
 * Yields `-1`, `0`, or `+1` for `l < r`, `l == r`, `l > r` respectively.
 * @param l Left operand.
 * @param r Right operand.
 */
#define SPACESHIP(l, r) \
    (l == r             \
        ? 0             \
        : l > r         \
            ? 1         \
            : -1)

#ifdef TARGET_NES
/**
 * @brief Atomic qualifier — `volatile` on the single-core NES.
 *
 * The NES is single-core with no concurrent access, so `volatile` is
 * sufficient to prevent the optimiser from reordering accesses.
 */
#define atomic volatile
#elif defined(__cplusplus)
/**
 * @brief Atomic qualifier — `volatile` on C++ desktop builds.
 *
 * C++ has no `_Atomic` type qualifier: GCC rejects it outright and
 * C++23's `<stdatomic.h>` only exposes the functional `_Atomic(T)`
 * form, not the prefix qualifier this macro is used as. The SDL3
 * emulator touches every `atomic`-qualified object from a single
 * thread (the renderer drains IRQs synchronously), so `volatile`
 * preserves the same ordering guarantees the NES build relies on.
 * The one genuinely cross-thread flag, `_vblank_flag`, is a real
 * `atomic_int` from `<stdatomic.h>` and does not use this macro.
 */
#define atomic volatile
#else
/** @brief Atomic qualifier — standard `_Atomic` on desktop C builds. */
#define atomic _Atomic
#endif

#ifdef __cplusplus
/**
 * @brief Scoped save/restore of N variables.
 *
 * `SHADOW(a, b, ...) { body }` snapshots each listed lvalue, runs the body
 * once, then restores every snapshot in reverse (LIFO) order as the block
 * exits — including on `break`/`return` out of the body. Arguments may be of
 * mixed types; `volatile`/`atomic` lvalues are read and written exactly once.
 * Supports up to 32 arguments (extend the SH_FE_/SH_ARG_N tables for more).
 *
 * @warning A bare `break`/`continue` inside the body binds to SHADOW's own
 *          hidden loop, not to any enclosing loop (same caveat as ::VRAM).
 */
template <class T>
struct shadow_guard {
    T& ref;
    std::remove_cv_t<T> saved;
    bool first = true;
    explicit shadow_guard(T& r) : ref(r), saved(r) {}
    ~shadow_guard() { ref = saved; }
    explicit operator bool() { const bool f = first; first = false; return f; }
};

#define SH_CAT_(a, b) a##b
#define SH_CAT(a, b)  SH_CAT_(a, b)
#define SH_UID(i)     SH_CAT(SH_CAT(_shadow_, __LINE__), SH_CAT(_, i))
#define SH_G(v, i)    for (::shadow_guard SH_UID(i){v}; SH_UID(i); )

#define SH_ARG_N(_1,_2,_3,_4,_5,_6,_7,_8,_9,_10,_11,_12,_13,_14,_15,_16,_17,_18,_19,_20,_21,_22,_23,_24,_25,_26,_27,_28,_29,_30,_31,_32,Nm,...) Nm
#define SH_RSEQ() 32,31,30,29,28,27,26,25,24,23,22,21,20,19,18,17,16,15,14,13,12,11,10,9,8,7,6,5,4,3,2,1,0
#define SH_NARG_(...) SH_ARG_N(__VA_ARGS__)
#define SH_NARG(...)  SH_NARG_(__VA_ARGS__, SH_RSEQ())

#define SH_FE_1(a)     SH_G(a, 0)
#define SH_FE_2(a,...) SH_G(a, 1) SH_FE_1(__VA_ARGS__)
#define SH_FE_3(a,...) SH_G(a, 2) SH_FE_2(__VA_ARGS__)
#define SH_FE_4(a,...) SH_G(a, 3) SH_FE_3(__VA_ARGS__)
#define SH_FE_5(a,...) SH_G(a, 4) SH_FE_4(__VA_ARGS__)
#define SH_FE_6(a,...) SH_G(a, 5) SH_FE_5(__VA_ARGS__)
#define SH_FE_7(a,...) SH_G(a, 6) SH_FE_6(__VA_ARGS__)
#define SH_FE_8(a,...) SH_G(a, 7) SH_FE_7(__VA_ARGS__)
#define SH_FE_9(a,...) SH_G(a, 8) SH_FE_8(__VA_ARGS__)
#define SH_FE_10(a,...) SH_G(a, 9) SH_FE_9(__VA_ARGS__)
#define SH_FE_11(a,...) SH_G(a, 10) SH_FE_10(__VA_ARGS__)
#define SH_FE_12(a,...) SH_G(a, 11) SH_FE_11(__VA_ARGS__)
#define SH_FE_13(a,...) SH_G(a, 12) SH_FE_12(__VA_ARGS__)
#define SH_FE_14(a,...) SH_G(a, 13) SH_FE_13(__VA_ARGS__)
#define SH_FE_15(a,...) SH_G(a, 14) SH_FE_14(__VA_ARGS__)
#define SH_FE_16(a,...) SH_G(a, 15) SH_FE_15(__VA_ARGS__)
#define SH_FE_17(a,...) SH_G(a, 16) SH_FE_16(__VA_ARGS__)
#define SH_FE_18(a,...) SH_G(a, 17) SH_FE_17(__VA_ARGS__)
#define SH_FE_19(a,...) SH_G(a, 18) SH_FE_18(__VA_ARGS__)
#define SH_FE_20(a,...) SH_G(a, 19) SH_FE_19(__VA_ARGS__)
#define SH_FE_21(a,...) SH_G(a, 20) SH_FE_20(__VA_ARGS__)
#define SH_FE_22(a,...) SH_G(a, 21) SH_FE_21(__VA_ARGS__)
#define SH_FE_23(a,...) SH_G(a, 22) SH_FE_22(__VA_ARGS__)
#define SH_FE_24(a,...) SH_G(a, 23) SH_FE_23(__VA_ARGS__)
#define SH_FE_25(a,...) SH_G(a, 24) SH_FE_24(__VA_ARGS__)
#define SH_FE_26(a,...) SH_G(a, 25) SH_FE_25(__VA_ARGS__)
#define SH_FE_27(a,...) SH_G(a, 26) SH_FE_26(__VA_ARGS__)
#define SH_FE_28(a,...) SH_G(a, 27) SH_FE_27(__VA_ARGS__)
#define SH_FE_29(a,...) SH_G(a, 28) SH_FE_28(__VA_ARGS__)
#define SH_FE_30(a,...) SH_G(a, 29) SH_FE_29(__VA_ARGS__)
#define SH_FE_31(a,...) SH_G(a, 30) SH_FE_30(__VA_ARGS__)
#define SH_FE_32(a,...) SH_G(a, 31) SH_FE_31(__VA_ARGS__)

#define SHADOW(...) SH_CAT(SH_FE_, SH_NARG(__VA_ARGS__))(__VA_ARGS__)

/**
 * @brief Save/restore of N variables across separate statements.
 *
 * `PRESERVE(a, b, ...)` snapshots each listed lvalue into typed stack
 * locals; a later `RESTORE(a, b, ...)` writes the snapshots back. Unlike
 * ::SHADOW the two calls are independent statements, so restoration can
 * happen at an arbitrary point rather than at the end of a block.
 *
 * Both calls must list the same variables (RESTORE needs the names to know
 * what to assign), and both must sit in the same scope (the snapshots are
 * block-scoped locals). An early `return`/`break` between the two simply
 * skips restoration, which is correct — the live values are being discarded.
 * `volatile`/`atomic` lvalues are read once at PRESERVE and written once at
 * RESTORE. Supports up to 32 arguments.
 *
 * @warning A given variable may only be PRESERVE'd once per scope; a second
 *          PRESERVE of the same name collides on the snapshot local.
 */
#define PR_SAVE(v) std::remove_cv_t<decltype(v)> SH_CAT(_preserve_, v) = v;
#define PR_REST(v) v = SH_CAT(_preserve_, v);

#define PR_FE_1(m,a)     m(a)
#define PR_FE_2(m,a,...) m(a) PR_FE_1(m,__VA_ARGS__)
#define PR_FE_3(m,a,...) m(a) PR_FE_2(m,__VA_ARGS__)
#define PR_FE_4(m,a,...) m(a) PR_FE_3(m,__VA_ARGS__)
#define PR_FE_5(m,a,...) m(a) PR_FE_4(m,__VA_ARGS__)
#define PR_FE_6(m,a,...) m(a) PR_FE_5(m,__VA_ARGS__)
#define PR_FE_7(m,a,...) m(a) PR_FE_6(m,__VA_ARGS__)
#define PR_FE_8(m,a,...) m(a) PR_FE_7(m,__VA_ARGS__)
#define PR_FE_9(m,a,...) m(a) PR_FE_8(m,__VA_ARGS__)
#define PR_FE_10(m,a,...) m(a) PR_FE_9(m,__VA_ARGS__)
#define PR_FE_11(m,a,...) m(a) PR_FE_10(m,__VA_ARGS__)
#define PR_FE_12(m,a,...) m(a) PR_FE_11(m,__VA_ARGS__)
#define PR_FE_13(m,a,...) m(a) PR_FE_12(m,__VA_ARGS__)
#define PR_FE_14(m,a,...) m(a) PR_FE_13(m,__VA_ARGS__)
#define PR_FE_15(m,a,...) m(a) PR_FE_14(m,__VA_ARGS__)
#define PR_FE_16(m,a,...) m(a) PR_FE_15(m,__VA_ARGS__)
#define PR_FE_17(m,a,...) m(a) PR_FE_16(m,__VA_ARGS__)
#define PR_FE_18(m,a,...) m(a) PR_FE_17(m,__VA_ARGS__)
#define PR_FE_19(m,a,...) m(a) PR_FE_18(m,__VA_ARGS__)
#define PR_FE_20(m,a,...) m(a) PR_FE_19(m,__VA_ARGS__)
#define PR_FE_21(m,a,...) m(a) PR_FE_20(m,__VA_ARGS__)
#define PR_FE_22(m,a,...) m(a) PR_FE_21(m,__VA_ARGS__)
#define PR_FE_23(m,a,...) m(a) PR_FE_22(m,__VA_ARGS__)
#define PR_FE_24(m,a,...) m(a) PR_FE_23(m,__VA_ARGS__)
#define PR_FE_25(m,a,...) m(a) PR_FE_24(m,__VA_ARGS__)
#define PR_FE_26(m,a,...) m(a) PR_FE_25(m,__VA_ARGS__)
#define PR_FE_27(m,a,...) m(a) PR_FE_26(m,__VA_ARGS__)
#define PR_FE_28(m,a,...) m(a) PR_FE_27(m,__VA_ARGS__)
#define PR_FE_29(m,a,...) m(a) PR_FE_28(m,__VA_ARGS__)
#define PR_FE_30(m,a,...) m(a) PR_FE_29(m,__VA_ARGS__)
#define PR_FE_31(m,a,...) m(a) PR_FE_30(m,__VA_ARGS__)
#define PR_FE_32(m,a,...) m(a) PR_FE_31(m,__VA_ARGS__)

#define PRESERVE(...) SH_CAT(PR_FE_, SH_NARG(__VA_ARGS__))(PR_SAVE, __VA_ARGS__)
#define RESTORE(...)  SH_CAT(PR_FE_, SH_NARG(__VA_ARGS__))(PR_REST, __VA_ARGS__)
#endif // __cplusplus

#if !defined(__cplusplus) && __STDC_VERSION__ < 202311L
#define auto __auto_type   // pre-C23: borrow the extension
#endif                     // C23+ / C++: native auto, no macro needed

#ifdef __cplusplus
/** @brief C linkage for symbols shared with hand-written/ca65 assembly. */
#define ASM_LINKAGE extern "C"
#else
#define ASM_LINKAGE extern
#endif

/**
 * @brief Clang-only function attribute biasing the optimiser toward small code.
 *
 * Applied to functions where ROM size matters more than speed. GCC
 * builds silently drop the attribute — expanding to nothing — because
 * it only exists in Clang/LLVM.
 */
#if defined(__clang__)
#define MINSIZE __attribute__((minsize))
#else
#define MINSIZE
#endif

/**
 * @brief Internal building block used inside ::CHARMAP.
 *
 * Expands to an assembler `.ifc` arm matching the character token
 * @p ch, emitting @p val as a single byte and terminating the macro.
 * Not intended to be used directly.
 *
 * @param ch  Bareword character token.
 * @param val Byte value to emit when @p ch is matched.
 */
#define CM(ch, val) \
"  .ifc \\c, " #ch "\n" \
"    .byte " #val "\n" \
"    .exitm\n" \
"  .endif\n"

#if defined(__NES__) || defined(TARGET_NES)
  #define _RODATA_SECTION ".pushsection .rodata\n"
  #define _SYM(name) #name
#elif defined(__APPLE__)
  #define _RODATA_SECTION ".pushsection __TEXT,__const\n"
  #define _SYM(name) "_" #name
#elif defined(_WIN32)
  #define _RODATA_SECTION ".pushsection .rdata,\"dr\"\n"
  #define _SYM(name) #name
#else
  #define _RODATA_SECTION ".pushsection .rodata\n"
  #define _SYM(name) #name
#endif

/**
 * @brief Defines a named character map used by ::MAPPED_STRING.
 *
 * @p __VA_ARGS__ is a sequence of ::CM entries; the macro wraps them
 * in an assembler macro `emit_char_<mapname>` that maps each source
 * character to a raw byte. Any character not present triggers an
 * assembler error.
 *
 * @note ::CM uses bareword tokens (e.g. `CM(M, 0x16)`), not character
 *       literals. This is deliberate: `'\c'` does not survive GAS's
 *       escape processing inside `.irpc`, so the value is passed
 *       unquoted and matched with `.ifc` against a bare token.
 *
 * @param mapname Identifier for the character map.
 */
#define CHARMAP(mapname, ...)                   \
__asm__(                                        \
".macro emit_char_" #mapname " c\n"             \
__VA_ARGS__                                     \
"  .error \"emit_char_" #mapname ": char not in charmap\"\n" \
".endm\n"                                       \
)

/**
 * @brief Emits a null-terminated string translated through a ::CHARMAP.
 *
 * The symbol is placed in the platform-appropriate read-only section
 * and made globally visible. The final byte is `0x00`.
 *
 * @param mapname Name of a previously declared ::CHARMAP.
 * @param name    Global symbol to define.
 * @param chars   Source characters to translate (bareword, not a string).
 */
#define NULL_TERMINATED_MAPPED_STRING(mapname, name, chars)     \
__asm__(                                        \
_RODATA_SECTION                                 \
".globl " _SYM(name) "\n"                       \
_SYM(name) ":\n"                                \
".irpc c, " #chars "\n"                         \
"  emit_char_" #mapname " \\c\n"                \
".endr\n"                                       \
".byte 0x00\n"                                  \
".popsection\n"                                 \
);                                              \

/**
 * @brief Emits a string translated through a ::CHARMAP, with a C-visible extern.
 *
 * Same semantics as ::NULL_TERMINATED_MAPPED_STRING, but also declares
 * `extern const u8 name[N]` where `N` is the unterminated source
 * length, so C code can index into the result with `sizeof(name)`.
 *
 * @param mapname Name of a previously declared ::CHARMAP.
 * @param name    Global symbol to define.
 * @param chars   Source characters to translate.
 */
#define MAPPED_STRING(mapname, name, chars)     \
__asm__(                                        \
_RODATA_SECTION                                 \
".globl " _SYM(name) "\n"                       \
_SYM(name) ":\n"                                \
".irpc c, " #chars "\n"                         \
"  emit_char_" #mapname " \\c\n"                \
".endr\n"                                       \
".byte 0x00\n"                                  \
".popsection\n"                                 \
);                                              \
ASM_LINKAGE const u8 name[sizeof(#chars) - 1]

/**
 * @brief Forward-declares a string without its terminator.
 * @param name  Global symbol to declare.
 * @param chars Source characters whose length determines the array size.
 */
#define EXTERN_STRING(name, chars)              \
ASM_LINKAGE const u8 name[sizeof(#chars) - 1]

/**
 * @brief Forward-declares a null-terminated string, sized to include the terminator.
 * @param name  Global symbol to declare.
 * @param chars Source characters whose length determines the array size.
 */
#define EXTERN_NULL_TERMINATED_STRING(name, chars)              \
ASM_LINKAGE const u8 name[sizeof(#chars)]

/**
 * @brief Expands to `obj, sizeof(obj)` for passing a buffer+length pair.
 * @param obj A C array or compound literal.
 */
#define SIZED_OBJ(obj) obj, sizeof(obj)

/**
 * @brief General-purpose byte copy from @p buffer into @p target with stride.
 *
 * Writes `buffer[i]` to `target[offset + i * step]` for `i` in
 * `[0, sBuffer)`. @p step is signed; a negative value walks backward
 * from @p offset. The caller must ensure every written index stays
 * inside the target buffer.
 *
 * @param target  Destination buffer.
 * @param offset  Starting index inside @p target.
 * @param buffer  Source byte array.
 * @param sBuffer Number of bytes to copy from @p buffer.
 * @param step    Signed stride in @p target between consecutive writes.
 */
void PopulateFromBuffer(u8* target, u16 offset,
                        const u8* buffer, u16 sBuffer, i16 step);

/**
 * @brief General-purpose fill from a provider function with stride.
 *
 * Writes `fn(i)` to `target[offset + i * step]` for `i` in `[0, amt)`.
 * @p step is signed; a negative value walks backward from @p offset.
 *
 * @param target Destination buffer.
 * @param offset Starting index inside @p target.
 * @param fn     Provider callback returning the byte to store at iteration `i`.
 * @param amt    Number of iterations to perform.
 * @param step   Signed stride in @p target between consecutive writes.
 */
void PopulateFromProvider(u8* target, u16 offset,
                          u8 (*fn)(u16), u16 amt, i16 step);



#endif
