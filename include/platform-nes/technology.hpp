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

#ifdef __cplusplus
/**
 * @brief Reads one byte from a memory-mapped I/O address.
 *
 * Typed replacement for the former `PEEK()` macro: a single `volatile`
 * load. `always_inline` collapses it to the identical 6502 codegen the
 * macro produced, but with real parameter/return types and none of the
 * macro's expansion pitfalls. The C build keeps the macro form (`#else`).
 *
 * @param addr Hardware address to read.
 * @return The byte currently stored at @p addr.
 */
inline __attribute__((always_inline))
u8 peek(const u16 addr) {
    return *reinterpret_cast<volatile const u8 *>(addr);
}

/**
 * @brief Writes one byte to a memory-mapped I/O address.
 *
 * Typed replacement for the former `POKE()` macro: a single `volatile`
 * store, inlined to identical codegen.
 *
 * @param addr Hardware address to write.
 * @param data Byte to store.
 */
inline __attribute__((always_inline))
void poke(const u16 addr, const u8 data) {
    *reinterpret_cast<volatile u8 *>(addr) = data;
}
#else
/** @brief C fallback: byte read via a `volatile const` dereference. */
#define PEEK(addr)       (*(volatile const unsigned char *)(addr))
/** @brief C fallback: byte write via a `volatile` store. */
#define POKE(addr, data) (*(volatile unsigned char *)(addr)) = (data)
#endif

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
#include <br0/tuple>

/**
 * @brief Scoped save/restore of N variables — truly variadic.
 *
 * `SHADOW(a, b, ...) { body }` snapshots each listed lvalue, runs the body
 * once, then restores every snapshot in reverse (LIFO) order as the block
 * exits — including on `break`/`return` out of the body. Arguments may be of
 * mixed types; `volatile`/`atomic` lvalues are read and written exactly once.
 *
 * Unlike the previous macro-unrolled form there is no fixed argument cap: the
 * snapshots live in a `br0::tuple`, so the count is bounded only by the
 * compiler's parameter-pack limits. We use `br0::tuple` rather than
 * `std::tuple` (which the llvm-mos freestanding library does not ship), so the
 * implementation is byte-for-byte the same on the NES and desktop toolchains.
 *
 * @warning A bare `break`/`continue` inside the body binds to SHADOW's own
 *          hidden loop, not to any enclosing loop (same caveat as ::VRAM).
 */
template <class... Ts>
struct shadow_scope {
    br0::tuple<Ts&...> refs;                    ///< the originals (pointers only)
    br0::tuple<std::remove_cv_t<Ts>...> saved;  ///< one read each at entry
    bool first = true;

    explicit shadow_scope(Ts&... rs) : refs(rs...), saved(rs...) {}
    ~shadow_scope() { restore(br0::index_sequence_for<Ts...>{}); }

    // One store, written as a discarded-value statement so the volatile
    // assignment's result is never "used" (avoids -Wdeprecated-volatile under
    // P1152, exactly like the longhand stores elsewhere in this header).
    template <class D, class S>
    static void store(D& dst, const S& src) { dst = src; }

    // LIFO restore to match the documented contract; each element is a single
    // write back through the original reference.
    template <std::size_t... I>
    void restore(br0::index_sequence<I...>) {
        constexpr std::size_t N = sizeof...(Ts);
        (store(br0::get<N - 1 - I>(refs), br0::get<N - 1 - I>(saved)), ...);
    }

    explicit operator bool() { const bool f = first; first = false; return f; }
};
template <class... Ts>
shadow_scope(Ts&...) -> shadow_scope<Ts...>;

#define SH_CAT_(a, b) a##b
#define SH_CAT(a, b)  SH_CAT_(a, b)

/**
 * @brief Open a SHADOW scope over the listed lvalues.
 *
 * Expands to a single-iteration `for` whose loop variable is a ::shadow_scope
 * (CTAD deduces the element types). The body runs once; on exit the scope's
 * destructor restores every snapshot.
 */
#define SHADOW(...)                                              \
    for (::shadow_scope SH_CAT(_shadow_, __LINE__){__VA_ARGS__}; \
         SH_CAT(_shadow_, __LINE__);)
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
