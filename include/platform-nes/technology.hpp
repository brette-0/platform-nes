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
 * - ::CHARMAP, ::STRING, ::STRING_NT — define per-project character maps and
 *   emit compile-time-translated strings as header-only `inline constexpr`
 *   `std::array`s in `.rodata`.
 * - ::PopulateFromBuffer / ::PopulateFromProvider — generic strided
 *   byte copies used by the video and audio subsystems.
 */
#pragma once

#include <intsh>
using namespace br0::intsh;
#include <type_traits>
#include <array>
#include <cstddef>


#ifdef TARGET_NES
/**
 * @brief Builds the body of a segment-placement keyword like ::fixed.
 *
 * `CREATE_SEGMENT_KEYWORD(name)` expands to the `__attribute__((section(...)))`
 * that pins a function or variable into `.prg_rom_<name>`, the linker section
 * this mapper's script (vrc1.ld) maps into a real PRG-ROM region. It is a
 * builder, not a keyword itself: the preprocessor can't register a new macro
 * name from inside another macro's expansion, so a call like
 * `CREATE_SEGMENT_KEYWORD(fixed)` can only ever be the *right-hand side* of a
 * keyword's definition, e.g.
 *
 *     #define fixed CREATE_SEGMENT_KEYWORD(fixed)
 *
 * — one such line per segment name, after which the bare word (`fixed`) is
 * usable everywhere in VRC1 code as a ::direct / ::absolute-style qualifier.
 *
 * Expands to nothing off-NES.
 */
#define CREATE_SEGMENT_KEYWORD(name) __attribute__((section(name)))
#else
#define CREATE_SEGMENT_KEYWORD(name)
#endif


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

#ifdef TARGET_NES
/**
 * @brief Pins a variable into 6502 zero page ("direct page").
 *
 * Forces the symbol into `.zp.bss`, which the linker routes to zero page, so
 * every access is a 2-byte/3-cycle direct-page instruction instead of the
 * 3-byte/4-cycle absolute form. Reserve it for the hottest mutable state
 * (cursors, counters, per-frame scratch): zero page is only 256 bytes and is
 * shared with the compiler's imaginary registers and the FamiStudio enclave,
 * so spend it deliberately. For zero-initialised / runtime-assigned objects —
 * an object with a static initialiser belongs in `.zp.data` instead.
 *
 * Expands to nothing off-NES (the desktop host has no zero page).
 */
#define direct __attribute__((section(".zp.bss")))
/**
 * @brief Forces a variable out of zero page, into absolute-addressed memory.
 *
 * llvm-mos's LTO zero-page allocator opportunistically hoists small globals
 * into zero page; `absolute` opts a symbol out by pinning it to `.rodata`, so a
 * read-only table that is merely walked can never evict hotter state or starve
 * the FamiStudio enclave out of page zero. Use it for the level RLE tables and
 * similar cold lookup data. Read-only data is always absolute-addressed, so
 * there is no separate ROM qualifier.
 *
 * Expands to nothing off-NES.
 */
#define absolute __attribute__((section(".rodata")))
#else
#define direct
#define absolute
#endif

#ifdef __cplusplus
#include <br0/tuple>

/**
 * @brief Write-only hardware register with a RAM shadow.
 *
 * Models a memory-mapped port that cannot be read back (e.g. the NES
 * PPUCTRL/PPUMASK registers). The RAM shadow is the readable copy, and because
 * the only write path also pokes the hardware, shadow and register can never
 * drift: `get()` returns the shadow, `set()` writes both.
 *
 * The value-like surface (`operator u8`, `operator=(u8)`) lets an instance be
 * used in expressions exactly like the bare `atomic u8` shadow it replaces, so
 * existing read-modify-write code compiles unchanged. Copy-construction takes a
 * snapshot *without* poking, while copy-assignment writes through — precisely
 * what ::SHADOW needs to save on entry and restore (re-poking) on exit.
 *
 * The shadow keeps the `atomic` qualifier the bare shadow had, so its ordering
 * guarantees survive for callers that touch it from an interrupt. The default
 * constructor is trivial, so instances live in zero-initialised `.bss` with no
 * startup constructor — identical placement to the `atomic u8` they replace.
 *
 * @tparam Addr Memory-mapped address backing this register.
 */
template <u16 Addr>
class wo_register {
    atomic u8 shadow_;
public:
    wo_register() = default;                                  ///< trivial: instance lives in .bss
    wo_register(const wo_register &o) : shadow_(o.shadow_) {} ///< snapshot, no poke

    u8   get() const          { return shadow_; }                  ///< read  = shadow
    void set(const u8 v)      { shadow_ = v; poke(Addr, v); }      ///< write = shadow + hardware
    void poke_only(const u8 v){ poke(Addr, v); }                    ///< write hardware only, no shadow update

    operator u8() const { return shadow_; }
    wo_register &operator=(const u8 v)           { set(v);         return *this; }
    wo_register &operator=(const wo_register &o) { set(o.shadow_); return *this; } ///< restore path
};

/**
 * @brief Zero-overhead bitmask wrapper for a scoped enum.
 *
 * A scoped enum (`enum class`) is the right tool for a set of flag bits: the
 * enumerators stay out of the surrounding namespace, so a common name like
 * `Clear` or `DONE` can't collide with anything. The cost is that a scoped enum
 * deliberately does *not* implicitly convert to its underlying integer, so
 * `a | b`, `mask & flag`, and `if (flags)` no longer compile -- you would have
 * to `static_cast` at every use, which is exactly the casting bloat we want to
 * avoid.
 *
 * `enum_flags<E>` is the storage type for such a flag register. It holds one
 * `std::underlying_type_t<E>` (a single byte for a `: u8` enum) and supplies the
 * bitwise operators plus a contextual `bool` test, so flag code reads like plain
 * integer code while every value remains type-checked against `E`. Because an
 * enumerator converts implicitly *into* an `enum_flags<E>` (but not the other
 * way), you still cannot accidentally mix two unrelated enums or leak a flag set
 * into arithmetic.
 *
 * It is a pure compile-time abstraction: no member is `virtual`, so there is no
 * vtable and no per-object vptr; `sizeof(enum_flags<E>) == sizeof(underlying)`.
 * Every operation is `constexpr` (hence inline), lowering to the identical 6502
 * `and`/`ora`/`eor` the raw byte math would emit. The default constructor is
 * trivial/zero, so an instance lives in zero-initialised `.bss` with no startup
 * constructor -- the same placement as the bare `atomic u8` it replaces.
 *
 * @note For an `atomic` (volatile) flag register, keep read-modify-write as a
 *       single `reg = reg | E::FLAG;` statement: the by-value operators read the
 *       register exactly once and the assignment writes it once, side-stepping
 *       the `-Wdeprecated-volatile` (P1152) compound-assignment pitfall.
 *
 * @tparam E A scoped enumeration whose enumerators are single-bit flag values.
 */
template <class E>
class enum_flags {
    using U = std::underlying_type_t<E>;
    U bits_{};
public:
    constexpr enum_flags() = default;                                  ///< trivial: lives in .bss
    constexpr enum_flags(E e) : bits_(static_cast<U>(e)) {}            ///< enumerator -> flag set
    constexpr explicit enum_flags(U raw) : bits_(raw) {}              ///< from a raw underlying value

    // Volatile interop: an `atomic enum_flags<E>` register is read once when
    // copied by value into the operators below, and written once on assignment.
    // Both stay single-access (no read-modify-write on the volatile object) so
    // the `-Wdeprecated-volatile` (P1152) pitfall never arises; the volatile
    // operator= returns void for the same reason (its result is never read back).
    // The volatile-source ctor / volatile operator= match copy signatures, so the
    // ordinary (non-volatile) copy ops are re-defaulted alongside them.
    constexpr enum_flags(const enum_flags &) = default;
    constexpr enum_flags &operator=(const enum_flags &) = default;
    enum_flags(const volatile enum_flags &o) : bits_(o.bits_) {}        ///< read a volatile register
    void operator=(const enum_flags &o) volatile { bits_ = o.bits_; }   ///< write a volatile register

    constexpr explicit operator bool() const { return bits_ != 0; }   ///< `if (flags & mask)`
    constexpr U raw()   const { return bits_; }                       ///< underlying integer
    constexpr E value() const { return static_cast<E>(bits_); }       ///< back to the enum type

    /// True iff every bit in @p m is set (membership test for a multi-bit mask).
    constexpr bool has(enum_flags m) const { return (bits_ & m.bits_) == m.bits_; }

    friend constexpr enum_flags operator|(enum_flags a, enum_flags b) { return enum_flags(static_cast<U>(a.bits_ | b.bits_)); }
    friend constexpr enum_flags operator&(enum_flags a, enum_flags b) { return enum_flags(static_cast<U>(a.bits_ & b.bits_)); }
    friend constexpr enum_flags operator^(enum_flags a, enum_flags b) { return enum_flags(static_cast<U>(a.bits_ ^ b.bits_)); }
    friend constexpr enum_flags operator~(enum_flags a)               { return enum_flags(static_cast<U>(~a.bits_)); }

    constexpr enum_flags &operator|=(enum_flags o) { bits_ = static_cast<U>(bits_ | o.bits_); return *this; }
    constexpr enum_flags &operator&=(enum_flags o) { bits_ = static_cast<U>(bits_ & o.bits_); return *this; }
    constexpr enum_flags &operator^=(enum_flags o) { bits_ = static_cast<U>(bits_ ^ o.bits_); return *this; }

    friend constexpr bool operator==(enum_flags, enum_flags) = default;   ///< also yields !=
};

namespace prsv {
    template <std::size_t... I, class... Ts>
    void save_impl(u8* snap, br0::index_sequence<I...>, Ts&... regs) {
        ((snap[I] = static_cast<u8>(regs)), ...);
    }

    template <std::size_t... I, class... Ts>
    void restore_impl(u8* snap, br0::index_sequence<I...>, Ts&... regs) {
        ((regs = snap[I]), ...);
    }

    /**
     * @brief Write shadow values of @p regs into the flat byte array @p snap.
     *
     * Called by ::PRESERVE. Reads each lvalue once through its shadow (no
     * hardware access) and stores the byte at the corresponding index.
     */
    template <class... Ts>
    void save(u8* snap, Ts&... regs) {
        save_impl(snap, br0::index_sequence_for<Ts...>{}, regs...);
    }

    /**
     * @brief Write bytes from @p snap back to the lvalues @p regs.
     *
     * Called by ::RESTORE. For ::wo_register lvalues this writes both the
     * shadow and the hardware port; for raw `atomic u8` lvalues it is a
     * plain volatile store.
     */
    template <class... Ts>
    void restore(u8* snap, Ts&... regs) {
        restore_impl(snap, br0::index_sequence_for<Ts...>{}, regs...);
    }
} // namespace prsv

/**
 * @brief Snapshot a register family into its paired static storage.
 *
 * A register family is a pair of definitions that live together:
 * @code
 *   #define MY_REGS         reg_a, reg_b, reg_c
 *   inline u8 MY_REGS_snapshot[3] = {};
 * @endcode
 *
 * `PRESERVE(MY_REGS)` saves the current shadow value of every listed
 * register into `MY_REGS_snapshot`. The `##` in the expansion prevents the
 * family name from being macro-expanded when building the snapshot symbol, so
 * the same symbol name is produced at both ::PRESERVE and ::RESTORE, even
 * across translation-unit boundaries (e.g. ISR in a different file).
 *
 * No hardware is read on save; only the ::wo_register shadows are accessed.
 *
 * @param name Bare identifier of the family macro (e.g. `APU_REGISTERS`).
 */
#define PRESERVE(name) ::prsv::save(name##_snapshot, name)

/**
 * @brief Restore a ::PRESERVE-d register family from static storage.
 *
 * Writes `name##_snapshot` back into every listed register. For
 * ::wo_register lvalues this pokes the hardware port and updates the shadow;
 * for raw `atomic u8` lvalues it is a plain volatile store.
 *
 * @param name Bare identifier of the family macro (e.g. `APU_REGISTERS`).
 */
#define RESTORE(name) ::prsv::restore(name##_snapshot, name)

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
 * @brief Compile-time support for ::CHARMAP / ::STRING.
 */
namespace nes_str {
    /**
     * @brief Fallback for a character with no ::CM entry.
     *
     * Declared but never defined, and not @c constexpr: naming it during the
     * constant evaluation of a ::STRING makes that evaluation non-constant,
     * so an unmapped character is a compile error pointing at the offending string.
     * Because a charmap is only ever evaluated at compile time, this is never
     * ODR-used at run time, so the missing definition never reaches the linker.
     */
    [[noreturn]] u8 unmapped(char c);

    /** @brief Map @p s[0 .. N-2] through @p Map into bytes (drops the literal NUL). */
    template <u8 (*Map)(char), std::size_t N>
    constexpr std::array<u8, N - 1> encode(const char (&s)[N]) {
        std::array<u8, N - 1> out{};
        for (std::size_t i = 0; i + 1 < N; ++i) out[i] = Map(s[i]);
        return out;
    }

    /** @brief As ::encode, but keeps a trailing 0x00 terminator. */
    template <u8 (*Map)(char), std::size_t N>
    constexpr std::array<u8, N> encode_nt(const char (&s)[N]) {
        std::array<u8, N> out{};
        for (std::size_t i = 0; i + 1 < N; ++i) out[i] = Map(s[i]);
        return out;
    }

    /** @brief ::SIZED_OBJ plumbing: a pointer+count that works for both a raw C
     *         array and a @c std::array (so existing call sites are unchanged). */
    template <class T, std::size_t N> constexpr const T*      data(const T (&a)[N]) { return a; }
    template <class T, std::size_t N> constexpr std::size_t   size(const T (&)[N])  { return N; }
    template <class A> constexpr auto data(const A& a) -> decltype(a.data()) { return a.data(); }
    template <class A> constexpr auto size(const A& a) -> decltype(a.size()) { return a.size(); }
}

/**
 * @brief Internal building block used inside ::CHARMAP.
 *
 * Expands to one arm of the charmap's @c constexpr lookup: when the input
 * character equals token @p ch, return byte @p val. ::CM entries are juxtaposed
 * (no commas) inside ::CHARMAP, exactly as before -- each is now an @c if
 * statement instead of a string-literal fragment.
 *
 * @param ch  Bareword character token (e.g. `M`); taken as `(#ch)[0]`.
 * @param val Byte value to return when @p ch is matched.
 */
#define CM(ch, val) if (_c == (#ch)[0]) return (u8)(val);

/**
 * @brief Defines a named character map used by ::STRING.
 *
 * Expands to a @c constexpr function `charmap_<mapname>(char)` whose body is the
 * juxtaposed ::CM arms. A character with no ::CM entry falls through to
 * ::nes_str::unmapped (see there) -- a compile error at the offending string.
 *
 * Being an ordinary @c constexpr (hence @c inline) function, a charmap can live
 * in a header and be included by any number of translation units: no assembler
 * macro, no per-TU re-emission, and no LTO "macro already defined" collision.
 *
 * @note ::CM still uses bareword tokens (`CM(M, 0xed)`); the token is stringized
 *       and its first character taken, so the authoring syntax is unchanged from
 *       the original assembler-based charmap.
 *
 * @param mapname Identifier for the character map.
 */
#define CHARMAP(mapname, ...)                   \
constexpr u8 charmap_##mapname(char _c) {       \
    __VA_ARGS__                                 \
    return ::nes_str::unmapped(_c);             \
}

/**
 * @brief Defines a string translated through a ::CHARMAP (no terminator).
 *
 * One header-only definition -- no separate `extern` declaration and no
 * definition TU to keep in sync. Each character of @p chars is mapped through
 * `charmap_<mapname>` at compile time, yielding an @c inline @c constexpr
 * `std::array<u8, N>` of the *unterminated* source length. Being an inline
 * variable, it can be defined in a header and included by any number of TUs;
 * the linker folds the copies to a single object in rodata. `sizeof(name)` (and
 * ::SIZED_OBJ) therefore yield the character count, and because it is @c
 * constexpr the value is usable in constant expressions in every includer.
 *
 * An unmapped character is a compile error (see ::nes_str::unmapped).
 *
 * @note Requires `charmap_<mapname>` to be in scope at the point of use (include
 *       the header that declares the ::CHARMAP).
 *
 * @param mapname Name of a previously declared ::CHARMAP.
 * @param name    Symbol to define.
 * @param chars   Source characters to translate (bareword, not a string).
 */
#define STRING(mapname, name, chars)            \
inline constexpr auto name = ::nes_str::encode<charmap_##mapname>(#chars)

/**
 * @brief Defines a string translated through a ::CHARMAP, with a 0x00 terminator.
 *
 * Identical to ::STRING but keeps a trailing 0x00, so the array is
 * `sizeof(#chars)` bytes (the source length including the terminator).
 *
 * @param mapname Name of a previously declared ::CHARMAP.
 * @param name    Symbol to define.
 * @param chars   Source characters to translate (bareword, not a string).
 */
#define STRING_NT(mapname, name, chars)         \
inline constexpr auto name = ::nes_str::encode_nt<charmap_##mapname>(#chars)

/**
 * @brief Expands to `pointer, count` for passing a buffer+length pair.
 *
 * Works for both a raw C array and a `std::array` (see ::nes_str::data /
 * ::nes_str::size), so ::STRING results and plain arrays are accepted alike.
 *
 * @param obj A C array or `std::array`.
 */
#define SIZED_OBJ(obj) ::nes_str::data(obj), ::nes_str::size(obj)

/**
 * @brief General-purpose byte copy from @p buffer into @p target with stride.
 *
 * Writes `buffer[i]` to `target[offset + i * step]` for `i` in
 * `[0, sBuffer)`. @p step is signed; a negative value walks backward
 * from @p offset. The caller must ensure every written index stays
 * inside the target buffer.
 *
 * The count and stride types are generic: pass `u8`/`i8` where the run is
 * short to keep the loop counter and comparison single-byte, or the wider
 * `u16`/`i16` for larger spans. The index arithmetic is evaluated in `int`
 * so a negative @p step is portable across the 16-bit (NES) and host builds.
 *
 * @tparam Count Unsigned integer type of @p sBuffer / the loop counter.
 * @tparam Step  Signed integer type of @p step.
 * @param target  Destination buffer.
 * @param offset  Starting index inside @p target.
 * @param buffer  Source byte array.
 * @param sBuffer Number of bytes to copy from @p buffer.
 * @param step    Signed stride in @p target between consecutive writes.
 */
template <typename Count, typename Step>
void PopulateFromBuffer(u8* target, const u16 offset,
                        const u8* buffer, const Count sBuffer, const Step step) {
    const auto base = target + offset;
    for (Count i = 0; i < sBuffer; ++i) base[static_cast<int>(i) * step] = buffer[i];
}

/**
 * @brief General-purpose fill from a provider function with stride.
 *
 * Writes `fn(i)` to `target[offset + i * step]` for `i` in `[0, amt)`.
 * @p step is signed; a negative value walks backward from @p offset.
 *
 * The iteration/index type (@p Idx, deduced from @p fn), the count type
 * (@p Count) and the stride type (@p Step) are rest generic, so a caller can
 * pass narrow integers (e.g. a `u8` provider with a `u8` count and unit
 * stride) and the loop control / callback argument stay single-byte. The
 * index multiply is evaluated in `int` for portable negative-stride support.
 *
 * @tparam Idx   Parameter type of @p fn (the value handed to the callback).
 * @tparam Count Unsigned integer type of @p amt / the loop counter.
 * @tparam Step  Signed integer type of @p step.
 * @param target Destination buffer.
 * @param offset Starting index inside @p target.
 * @param fn     Provider callback returning the byte to store at iteration `i`.
 * @param amt    Number of iterations to perform.
 * @param step   Signed stride in @p target between consecutive writes.
 */
template <typename Idx, typename Count, typename Step>
void PopulateFromProvider(u8* target, const u16 offset,
                          u8 (*fn)(Idx), const Count amt, const Step step) {
    const auto base = target + offset;
    for (Count i = 0; i < amt; ++i)
        base[static_cast<int>(i) * step] = fn(static_cast<Idx>(i));
}
