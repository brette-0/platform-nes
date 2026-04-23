/**
 * @file technology.h
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
#else
/** @brief Atomic qualifier — standard `_Atomic` on desktop builds. */
#define atomic _Atomic
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
 * `extern const uint8_t name[N]` where `N` is the unterminated source
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
extern const uint8_t name[sizeof(#chars) - 1]

/**
 * @brief Forward-declares a string without its terminator.
 * @param name  Global symbol to declare.
 * @param chars Source characters whose length determines the array size.
 */
#define EXTERN_STRING(name, chars)              \
extern const uint8_t name[sizeof(#chars) - 1]

/**
 * @brief Forward-declares a null-terminated string, sized to include the terminator.
 * @param name  Global symbol to declare.
 * @param chars Source characters whose length determines the array size.
 */
#define EXTERN_NULL_TERMINATED_STRING(name, chars)              \
extern const uint8_t name[sizeof(#chars)]

/**
 * @brief Expands to `obj, sizeof(obj)` for passing a buffer+length pair.
 * @param obj A C array or compound literal.
 */
#define SIZED_OBJ(obj) obj, sizeof(obj)

#include <stdint.h>

/**
 * @brief General-purpose byte copy from @p buffer into @p target with stride.
 *
 * Writes `buffer[i]` to `target[offset + i * step]` for `i` in
 * `[0, sBuffer)`. @p step is signed; a negative value walks backward
 * from @p offset. The caller must ensure every written index stays
 * inside the target buffer.
 *
 * @note On SDL3 builds, if @p target is `&oamBuffer`, the call is
 *       routed through the OAM growth handler instead of being a
 *       straight byte copy.
 *
 * @param target  Destination buffer.
 * @param offset  Starting index inside @p target.
 * @param buffer  Source byte array.
 * @param sBuffer Number of bytes to copy from @p buffer.
 * @param step    Signed stride in @p target between consecutive writes.
 */
void PopulateFromBuffer(uint8_t* target, uint16_t offset,
                        const uint8_t* buffer, uint16_t sBuffer, int16_t step);

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
void PopulateFromProvider(uint8_t* target, uint16_t offset,
                          uint8_t (*fn)(uint16_t), uint16_t amt, int16_t step);

#endif
