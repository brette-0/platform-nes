#ifndef TECHNOLOGY_H
#define TECHNOLOGY_H

#define PEEK(addr) (*(volatile const unsigned char *)(addr))
#define POKE(addr, data) (*(volatile unsigned char *)(addr)) = data

#define SPACESHIP(l, r) \
    (l == r             \
        ? 0             \
        : l > r         \
            ? 1         \
            : -1)

#ifdef TARGET_NES
#define atomic volatile
#else
#define atomic _Atomic
#endif

// Clang/LLVM-only function attribute that biases the optimizer toward
// small code. Used for NES builds where ROM space is tight; GCC
// ignores it (with a warning) on desktop builds, so we strip it there.
#if defined(__clang__)
#define MINSIZE __attribute__((minsize))
#else
#define MINSIZE
#endif

#define CM(ch, val) \
"  .elseif \\c == " #ch "\n" \
"    .byte " #val "\n"

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

#define CHARMAP(mapname, ...)                   \
__asm__(                                        \
".macro emit_char_" #mapname " c\n"             \
"  .if 0\n"                                     \
__VA_ARGS__                                     \
"  .else\n"                                     \
"    .byte \\c\n"                               \
"  .endif\n"                                    \
".endm\n"                                       \
)

#define NULL_TERMINATED_MAPPED_STRING(mapname, name, chars)     \
__asm__(                                        \
_RODATA_SECTION                                 \
".globl " _SYM(name) "\n"                       \
_SYM(name) ":\n"                                \
".irpc c, " #chars "\n"                         \
"  emit_char_" #mapname " '\\c'\n"              \
".endr\n"                                       \
".byte 0x00\n"                                  \
".popsection\n"                                 \
);                                              \

#define MAPPED_STRING(mapname, name, chars)     \
__asm__(                                        \
_RODATA_SECTION                                 \
".globl " _SYM(name) "\n"                       \
_SYM(name) ":\n"                                \
".irpc c, " #chars "\n"                         \
"  emit_char_" #mapname " '\\c'\n"              \
".endr\n"                                       \
".byte 0x00\n"                                  \
".popsection\n"                                 \
);                                              \
extern const uint8_t name[sizeof(#chars) - 1]

#define EXTERN_STRING(name, chars)              \
extern const uint8_t name[sizeof(#chars) - 1]

#define EXTERN_NULL_TERMINATED_STRING(name, chars)              \
extern const uint8_t name[sizeof(#chars)]

#define SIZED_OBJ(obj) obj, sizeof(obj)

#include <stdint.h>

/**
 * General-purpose byte copy from buffer to (target + offset) with stride.
 * Writes buffer[i] to target[offset + i * step] for i in [0, sBuffer).
 * `step` is signed; a negative value walks backward from `offset`. The caller
 * must ensure all written indices stay within the target buffer.
 * On SDL3 builds, if `target` is &oamBuffer, routing switches to the
 * OAM growth handler.
 */
void PopulateFromBuffer(uint8_t* target, uint16_t offset,
                        const uint8_t* buffer, uint16_t sBuffer, int16_t step);

/**
 * General-purpose fill from a provider function with stride.
 * Writes fn(i) to target[offset + i * step] for i in [0, amt).
 * `step` is signed; a negative value walks backward from `offset`.
 */
void PopulateFromProvider(uint8_t* target, uint16_t offset,
                          uint8_t (*fn)(uint16_t), uint16_t amt, int16_t step);

#endif