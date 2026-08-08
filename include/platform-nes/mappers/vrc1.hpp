/**
 * @file vrc1.hpp
 * @brief VRC1 mapper: PRG/CHR bank switching and the ::fixed segment keyword.
 *
 * VRC1 exposes three switchable 8 KiB PRG-ROM windows (::VRC1::window1Control,
 * ::VRC1::window2Control, ::VRC1::window3Control) and two independent 4 KiB
 * CHR-ROM windows (::VRC1::chr0Control, ::VRC1::chr1Control, with the shared
 * high bit in ::VRC1::chrHighBits). ::VRC1::Long and ::VRC1::SwitchCHRBank
 * are the safe entry points for switching either; ::fixed pins code that
 * must survive a bankswitch into the mapper's always-mapped $E000-$FFFF
 * window.
 *
 * This module is a class (::VRC1), not a namespace, matching ::MMC3's own
 * shape: every member is static, and the class itself is non-instantiable
 * (all constructors deleted) -- the NES target has exactly one physical VRC1
 * chip on the cartridge, so there is never a real object to construct there.
 * See mmc3.hpp's own file comment for the fuller rationale (a future
 * emulated-cartridge model gets somewhere to hold real per-instance state
 * without colliding names with this NES-side, state-free version).
 */
#pragma once

#include <intsh>
#include <utility>

#include <platform-nes/technology.hpp>
#ifndef TARGET_NES
#include <platform-nes/video.hpp>
#endif

using namespace br0::intsh;

/*
 *  no long reads or long writes (not clean in c++) instead we favor longcalls and require get methods to be created
 *  I don't expect there to be much overhead to this, it will suggest encapsulation to the user
 */

/**
 * @brief Pins a function or variable into VRC1's fixed PRG-ROM bank ($E000-$FFFF).
 *
 * Backed by `.prg_rom_fixed`, the only segment vrc1.ld currently wires to a
 * real, always-mapped region -- see the file comment at the top of vrc1.ld.
 * Expands to nothing off-NES.
 */
#define fixed CREATE_SEGMENT_KEYWORD(".prg_rom_fixed")

/**
 * @brief VRC1 mapper scoping class: PRG/CHR bank registers and the
 *        banked-call scaffolding. See this file's own header comment.
 *
 * Every member is `static`; the class itself is non-instantiable (all
 * constructors deleted) since the NES target never needs -- and must never
 * create -- an object of it.
 */
class VRC1 {
public:
    VRC1() = delete;
    VRC1(const VRC1 &) = delete;
    VRC1 &operator=(const VRC1 &) = delete;

    /**
     * @brief VRC1's three switchable 8 KiB PRG-select registers ($8000/$A000/$C000).
     *
     * Not `const`: a "write-only register" that can never be written defeats the
     * point. Defined (not just declared) in vrc1.cpp, where the default-bank
     * boot-time init also lives.
     */
    static tech::wo_register<0x8000> window1Control; ///< PRG-select, window 1 ($8000-$9FFF). See above.
    static tech::wo_register<0xa000> window2Control; ///< PRG-select, window 2 ($A000-$BFFF). See ::window1Control.
    static tech::wo_register<0xc000> window3Control; ///< PRG-select, window 3 ($C000-$DFFF). See ::window1Control.

    /**
     * @brief Writes @p ctx into a VRC1 PRG-select register, banking in that window.
     *
     * Thin, named wrapper over ::tech::wo_register's write path (updates the RAM
     * shadow and pokes hardware together, so the two can never drift). @p reg is
     * taken by reference -- by value, the write would land on a throwaway copy:
     * the hardware poke would still happen (the address is a template constant,
     * not tied to which copy calls it), but the shadow update would be lost,
     * leaving the original register's tracked state stale.
     *
     * Not used by ::_reset (vrc1.cpp): that runs before .bss has been zeroed,
     * so window1Control/2/3's shadows aren't valid to write yet regardless of
     * how this compiles -- see ::_reset's own comment for why it pokes hardware
     * directly instead.
     */
    template <u16 addr>
    static constexpr void SwitchBank(tech::wo_register<addr> &reg, u8 ctx) {
#ifdef TARGET_NES
        reg = ctx;
#else
        // Off-NES there is no PRG-ROM to bank (::Long already collapses to a
        // plain call there -- see its own comment), so a direct SwitchBank call
        // on window1Control/2/3 has nothing to do. Unlike ::SwitchCHRBank, this
        // has no bearing on the emu PPU's tile fetch, so there's no shadow state
        // to update either -- just don't poke real hardware.
        (void)reg;
        (void)ctx;
#endif
    }

    /**
     * @brief Calls @p fn with one of VRC1's three switchable windows temporarily
     *        re-banked, then restores whatever was banked there before returning.
     *
     * @p window selects *which* window gets switched -- 0 for window1Control
     * ($8000-$9FFF), 1 for window2Control ($A000-$BFFF), 2 for window3Control
     * ($C000-$DFFF) -- defaulting to window 0 when omitted. Lives in the fixed
     * bank (::fixed) so it's always reachable regardless of what's currently
     * banked in elsewhere.
     *
     * @p fn may be any callable -- a captureless function pointer or a lambda
     * (captures included, for passing arguments) -- so long as `fn()` is valid
     * and convertible to @p TReturn.
     *
     * @note At the project's current 32 KiB PRG-ROM there is exactly one valid
     *       bank per window, so the bank written here is that window's own
     *       boot-time default (see vrc1.cpp) -- today this call reasserts the
     *       window's already-active bank rather than switching to a different
     *       one. Once real multi-bank content exists per window (the
     *       `.prg_rom_0` / `.prg_rom_1` / `.prg_rom_2` tagging vrc1.ld's file
     *       comment calls out as future work), an explicit target-bank
     *       parameter will need to be added here.
     *
     * @tparam TReturn Result type of the call; not deducible from @p fn, so
     *                 specify it explicitly at the call site, e.g. `Long<int>(...)`.
     * @param fn     Callable to invoke once the selected window is banked in.
     * @param window Which window to switch: 0, 1, or 2 (default 0); ignored off-NES.
     *
     * @note Off-NES there is no PRG-ROM to bank -- window1Control/2/3 aren't even
     *       defined there (vrc1.cpp is an NES-only source file) -- so this
     *       collapses to a plain `return fn();`, skipping ::Detail::CallInWindow
     *       entirely rather than binding a reference to an undefined extern.
     */
    template <typename TReturn, typename TFunc>
    static fixed TReturn Long(TFunc fn, u8 window = 0);

    /*
     * CHR bankswitching. VRC1 has two independent 4 KiB CHR windows -- PPU
     * $0000-$0FFF ("pattern table 0") and $1000-$1FFF ("pattern table 1"),
     * matching this project's own terminology in video.hpp (::PatternTables,
     * ::CHR_TILES_PER_TABLE) -- each showing one of up to 32 physical 4 KiB
     * banks (128 KiB CHR-ROM max). Each bank number is 5 bits: 4 low bits
     * written to that window's own select register ($E000 / $F000), plus 1
     * high bit packed into a register the two windows *share* ($9000: bit 0
     * for pattern table 0, bit 1 for pattern table 1) -- so setting one
     * window's high bit is a read-modify-write against the other window's
     * current bit, not a plain overwrite.
     */

    /**
     * @brief VRC1's CHR-select registers ($9000/$E000/$F000).
     *
     * chrHighBits ($9000) is shared between both pattern tables (bit 0 = table
     * 0's bank bit 4, bit 1 = table 1's), which is exactly what ::tech::wo_register's
     * shadow exists for: ::SwitchCHRBank reads it back to flip only the calling
     * window's own bit, never the other window's. chr0Control ($E000) and
     * chr1Control ($F000) hold each table's low 4 bank bits. Not `const`,
     * same reasoning as window1Control etc.
     */
    static tech::wo_register<0x9000> chrHighBits; ///< Shared high bank bit for both pattern tables. See above.
    static tech::wo_register<0xe000> chr0Control; ///< CHR-select, pattern table 0 ($0000-$0FFF). See ::chrHighBits.
    static tech::wo_register<0xf000> chr1Control; ///< CHR-select, pattern table 1 ($1000-$1FFF). See ::chrHighBits.

#ifndef TARGET_NES
    /**
     * @brief Emu-side CHR bank storage: index 0 = chr0Control (pattern table 0,
     *        PPU $0000-$0FFF), index 1 = chr1Control (pattern table 1, PPU
     *        $1000-$1FFF). Each holds the full 5-bit bank number directly --
     *        off-NES there's no shared chrHighBits port to split it across, so
     *        ::SwitchCHRBank just stores the whole value here. Consulted by
     *        ::GetTileLMA to resolve a tile fetch. Defined in the emu-side
     *        vrc1.cpp (src/emu/mappers/vrc1.cpp), not the NES-side one.
     */
    static u8 chrBanks[2];

    /**
     * @brief Translates a tile's PPU pattern-table address (0x0000-0x1FFF) into
     *        a byte offset in the flat CHR-ROM image, resolving it through
     *        ::chrBanks.
     *
     * Library-internal: the only caller is this module's own strong
     * `ppu::ResolveTile` override (src/emu/mappers/vrc1.cpp), which the emu
     * PPU (src/emu/ppu.cpp) calls unconditionally for every tile fetch -- see
     * that function's own doc comment (video.hpp) for the weak/strong
     * relationship. Public rather than private purely because its caller is
     * a free function, not a member of this class -- C++ access control has
     * no "same translation unit" exemption. A game selects banks through
     * ::SwitchCHRBank instead.
     */
    static u32 GetTileLMA(u16 tileVMA);
#endif

    /**
     * @brief Selects which 4 KiB CHR-ROM bank appears in a VRC1 pattern table.
     *
     * Single template, mirroring ::SwitchBank's shape: @p addr (deduced from
     * @p reg) picks between chr0Control ($E000, PPU $0000-$0FFF / pattern table
     * 0) and chr1Control ($F000, PPU $1000-$1FFF / pattern table 1).
     *
     * On NES: which bit of the *shared* chrHighBits register this call is
     * allowed to touch -- see the comment above chrHighBits. The read-modify-
     * write against chrHighBits' shadow is what keeps the other window's high
     * bit untouched. Off-NES: there's no hardware to poke, so this instead
     * records the full bank number into ::chrBanks for ::GetTileLMA to resolve.
     *
     * @param reg  chr0Control or chr1Control; any other instantiation is a
     *             compile error.
     * @param bank Target bank, 0-31.
     */
    template <u16 addr>
    static constexpr void SwitchCHRBank(tech::wo_register<addr> &reg, u8 bank) {
        static_assert(addr == 0xe000 || addr == 0xf000,
                      "SwitchCHRBank only valid for chr0Control ($E000) or chr1Control ($F000)");
#ifdef TARGET_NES
        constexpr u8 bit = (addr == 0xe000) ? 0 : 1;
        reg = bank & 0x0F;
        chrHighBits = (chrHighBits.get() & ~(u8(1) << bit)) | (((bank >> 4) & 1) << bit);
#else
        (void)reg;
        constexpr u8 idx = (addr == 0xe000) ? 0 : 1;
        chrBanks[idx] = bank & 0x1F;
        ++ppu::chrGeneration;
#endif
    }

    /*
     * BANKED CALL scaffolding (see BANKED_CALL_THEORY.txt). Kept here rather
     * than in a separate header because bank_layout<Tag> specializations are
     * meant to sit next to the hardware facts (window1Control etc.) they
     * describe.
     */

    /**
     * @brief One physical bank/segment's location in the linked ROM image.
     *
     * rom_address is LOADADDR()-derived (physical position in the ROM file),
     * deliberately not the CPU-visible VMA: on a switchable window, every bank
     * that ever aliases onto that window shares the same VMA, so only LOADADDR()
     * can tell banks apart.
     */
    struct section_t {
        u32 rom_address;
        u32 size;
    };

    /**
     * @brief Hardware facts for one tag/domain. Primary template deliberately
     *        undefined: an un-tagged Tag is a compile error, not a silent default.
     *
     * A non-fixed specialization (always_mapped = false) must also provide
     * `static section_t section()` -- a FUNCTION, not a data member, so both
     * shapes below share one calling convention (`L::section()`) at the
     * Call<Fn>/CallBlock<Fn> call site.
     *
     * PREFER `static constexpr section_t section() { return {addr, size}; }`,
     * a literal the human enters by hand, matched against a linker script rule
     * that gives this domain its OWN dedicated MEMORY region at that SAME known
     * ORIGIN on purpose (see bank_layout<window_test_tag> in vrc1.cpp, and
     * prg_rom_window_test in vrc1.ld, for a worked example, including a
     * link-time ASSERT that catches the two drifting apart). This is a real
     * compile-time constant, so CallInSection's window/bank resolution folds
     * away entirely -- measured equivalent to a hand-supplied ::Long window
     * argument, i.e. free.
     *
     * A non-constexpr `static section_t section()`, computing the value at
     * runtime from a linker-PROVIDE()'d symbol's address (LOADADDR() is a
     * link-time fact; converting a linker symbol's address to an integer is not
     * a constant expression in standard C++), is the fallback for a domain that
     * genuinely can't be given a fixed address -- e.g. sharing a region with
     * other, unrelated content, so its final placement depends on everything
     * else placed before it. MEASURED COST: ~120 bytes of real 6502 code per
     * Call<Fn>/CallBlock<Fn> call site (a 32-bit subtract/shift-divide/branch),
     * because the compiler doesn't fold the computation back down to the
     * compile-time constant it actually is for any one Call<Fn> instantiation
     * -- not a byte-width issue (confirmed: narrowing to u16 barely helps), a
     * genuine missed optimization through the runtime section_t indirection.
     * Reach for this only when a dedicated region truly isn't possible; it is
     * NOT the default. (If ever needed: a stored, dynamically-initialized data
     * member is NOT a safe alternative to computing fresh in the function body
     * -- unprioritized dynamic initializers run AFTER every explicitly
     * prioritized __attribute__((constructor(N))) on this toolchain, so an
     * early constructor calling Call<Fn> could read the member before its own
     * initializer has run.)
     */
    template <typename Tag> struct bank_layout;

    /// Key for vrc1.hpp's own always-mapped fixed bank ($E000-$FFFF, see ::fixed).
    /// Named fixed_bank_tag, not fixed_tag: VRC1_BANKED()'s tag argument gets
    /// macro-expanded before tag##_tag pastes it (so a #define'd tag can be
    /// forwarded through config headers, see VRC1_BANKED_IMPL below) -- but that
    /// means the bare word `fixed` can never be used as a tag token here, since
    /// `fixed` is ITSELF already a macro in this file (CREATE_SEGMENT_KEYWORD),
    /// and would expand before pasting, producing garbage. VRC1_BANKED() call
    /// sites for this domain must use the tag `fixed_bank`, not `fixed`.
    struct fixed_bank_tag {};

    /// Which bank_layout<Tag> a specific tagged function was registered under.
    /// One specialization per VRC1_BANKED()/VRC1_BANKED_EXTERN() call site,
    /// generated by those macros -- never written by hand. Primary template
    /// deliberately undefined, same reasoning as bank_layout<Tag>.
    template <auto Fn> struct bank_of;

    /// Return-type extraction for a plain function pointer, used by Call<Fn>.
    template <typename T> struct function_traits;

    /**
     * @brief Calls a specific, compile-time-known VRC1_BANKED() function,
     *        resolving its bank from bank_of<Fn> automatically -- no manual
     *        window/bank argument, unlike ::Long.
     *
     * @tparam Fn Address of a VRC1_BANKED()/VRC1_BANKED_EXTERN()-tagged
     *            function -- supplied explicitly, e.g. `Call<LoadLevelChunk>(chunkId)`.
     */
    template <auto Fn, typename... Args>
    static fixed auto Call(Args &&...args) -> typename function_traits<decltype(Fn)>::return_type;

    /**
     * @brief Runs an arbitrary block under Fn's resolved window, instead of
     *        calling Fn itself -- for generic code that takes a runtime
     *        callback parameter (see BANKED_CALL_THEORY.txt's
     *        PopulateFromProvider example). Fn only anchors which bank_of<>
     *        entry to resolve; the block may capture freely.
     */
    template <auto Fn, typename Block>
    static fixed auto CallBlock(Block &&block) -> decltype(block());

private:
    /**
     * @brief Implementation details not part of VRC1's public interface.
     *        Private nested class purely as a namespacing device -- a class
     *        cannot nest a plain `namespace`, so this plays the same role
     *        the free-standing `namespace vrc1_detail` played before.
     */
    struct Detail {
        /// The three switchable windows' shared geometry: each is WINDOW_SIZE
        /// bytes, starting at WINDOW_BASE ($8000, window1Control's own base) and
        /// running back to back through window3Control -- see the MEMORY block
        /// comment in vrc1.ld. rom_address is an absolute LOADADDR()-derived
        /// address, so it must be rebased against WINDOW_BASE before dividing;
        /// dividing the raw absolute address would be off by WINDOW_BASE/WINDOW_SIZE.
        static constexpr u16 kWindowBase = 0x8000;
        static constexpr u16 kWindowSize = 0x2000;

        /**
         * @brief Implementation of ::Long once the target register is resolved.
         *
         * Not for direct use -- call ::Long. Saves @p reg (via ::SHADOW, which
         * restores on any exit path, including a `return` straight out of the
         * body below), switches it to @p bank, invokes @p fn, and writes the
         * prior value back as the scope closes.
         *
         * [[gnu::noinline]] is LOAD-BEARING, not a size/style choice: `fixed`
         * only places an out-of-line SYMBOL, and a template this small is an
         * easy inlining target at -Os -- confirmed empirically, it was getting
         * fully inlined into EVERY caller, with no separate out-of-line copy
         * ever existing at all. That's silently fatal the moment a caller
         * whose OWN code lives inside the very window being switched (e.g. a
         * bank3-resident function reaching another function that also lives in
         * window3Control's range) inlines this: the restore-after-return half
         * of this function's own body ends up physically embedded inside the
         * window that just got switched AWAY from the caller's own bank, so by
         * the time the inner call returns, that restore code isn't the bank
         * mapped in anymore -- the CPU resumes into whatever unrelated bytes
         * the OTHER bank happens to have at that address. Confirmed as a real,
         * reproduced crash (illegal opcodes immediately after the inner call
         * returns), not a theoretical concern -- see BANKED_CALL_THEORY.txt.
         * noinline forces a real, separate, permanently-fixed-bank-resident
         * symbol to exist, so this function's own execution -- switch, call,
         * restore -- never depends on which window is currently mapped in
         * anywhere else, including the caller's own.
         */
        template <typename TReturn, u16 addr, typename TFunc>
        [[gnu::noinline]] static fixed TReturn CallInWindow(tech::wo_register<addr> &reg, u8 bank, TFunc fn) {
            SHADOW(reg) {
                SwitchBank(reg, bank);
                return fn();
            }
            __builtin_unreachable(); // SHADOW's scope always runs its body
                                     // exactly once and returns; the compiler
                                     // can't see that through the
                                     // single-iteration for loop.
        }

        /**
         * @brief Like CallInWindow, but for a domain spanning TWO adjacent
         *        windows simultaneously (BANKED_CALL_THEORY.txt's "oversized
         *        domain" case, Phase 4) -- switches windowIndexBase to bankBase
         *        AND windowIndexBase+1 to bankBase+1 (nested SHADOW, same
         *        save/restore composition CallInWindow already gives a single
         *        register), runs @p fn with BOTH mapped, restores both on any
         *        exit path. Only the two pairings this project's own windows
         *        allow exist: (window1,window2) or (window2,window3) -- VRC1's
         *        three windows are physically contiguous, so N adjacent ones can
         *        be driven as one flat region, but there are only two possible
         *        adjacent PAIRS to enumerate, not a general N-window loop.
         */
        // [[gnu::noinline]]: same load-bearing reason as CallInWindow's own --
        // see its comment. Doubly true here, since this function's own body
        // calls CallInWindow twice more (nested), all of which needs to stay a
        // real, separate, permanently-fixed-bank-resident unit regardless of
        // which switchable bank any CALLER of this function happens to live in.
        template <typename TReturn, typename TFunc>
        [[gnu::noinline]] static fixed TReturn CallInWindows2(u8 windowIndexBase, u8 bankBase, TFunc fn) {
            switch (windowIndexBase) {
                case 0:
                    return CallInWindow<TReturn>(window1Control, bankBase, [&]() -> TReturn {
                        return CallInWindow<TReturn>(window2Control, static_cast<u8>(bankBase + 1), fn);
                    });
                case 1:
                default:
                    return CallInWindow<TReturn>(window2Control, bankBase, [&]() -> TReturn {
                        return CallInWindow<TReturn>(window3Control, static_cast<u8>(bankBase + 1), fn);
                    });
            }
        }

        /**
         * @brief Resolves a section_t (rom_address+size) to whatever register(s)
         *        writes VRC1 needs, then runs @p fn.
         *
         * WINDOW is always derived from the low 16 bits of rom_address (the real
         * CPU-visible VMA -- exactly what an absolute JSR to this content would
         * truncate to on real hardware, since 6502 relocations only ever carry
         * 16 bits regardless of what the linker privately tracked). BANK is
         * separate: for content sharing the project's original, un-encoded
         * switchable region (rom_address fits in 16 bits, no high bits set),
         * bank == window index -- the project's long-standing "one bank per
         * window" degenerate default (::_reset boot-init, ::Long's own doc
         * comment). For an EXPLICIT alternate bank (bank_layout<Tag>::section()
         * hand-encodes its real bank number in rom_address's high bits, matching
         * prg_rom_bankN's ORIGIN convention in vrc1.ld -- see that file's Phase 3
         * comment), the encoded number wins. This lets old, un-encoded tags
         * (window_test_tag) keep working unchanged while new, explicit-bank tags
         * (bank3_test_tag) get a real, distinct bank -- see BANKED_CALL_THEORY.txt.
         *
         * WINDOW COUNT (Phase 4) comes from `size`, ceil-divided by kWindowSize.
         * Unlike rom_address, size is genuinely link-computed for an oversized
         * domain (SIZEOF() of however much content actually got tagged into it
         * -- nobody hand-declares this, the whole point of the feature), so
         * this division is NOT a compile-time constant the way Phase 2/3's
         * single-window resolution is -- it costs real runtime bytes, unlike
         * everything else CallInSection does. That's an accepted, necessary
         * cost of dynamic sizing, not a missed optimization to chase; see
         * BANKED_CALL_THEORY.txt's Phase 4 notes.
         *
         * [[gnu::noinline]]: same load-bearing reason as CallInWindow's own --
         * see its comment. This is the entry point Call<Fn>/CallBlock<Fn>
         * actually invoke, so it's the one that matters most: without this,
         * confirmed empirically, no separate out-of-line copy of this
         * machinery ever existed at all -- it got fully inlined into every
         * caller, silently defeating `fixed` the moment a caller living in a
         * switchable bank called into another function sharing that same
         * window (a real, reproduced crash -- illegal opcodes immediately
         * after the inner call returns -- not a theoretical concern).
         */
        template <typename TReturn, typename TFunc>
        [[gnu::noinline]] static fixed TReturn CallInSection(const section_t &section, TFunc fn) {
            const u16 vma = static_cast<u16>(section.rom_address);
            const u8 windowIndex = static_cast<u8>((vma - kWindowBase) / kWindowSize);
            const u8 explicitBank = static_cast<u8>(section.rom_address >> 16);
            const u8 bank = explicitBank != 0 ? explicitBank : windowIndex;
            const u8 windowCount = static_cast<u8>((section.size + kWindowSize - 1) / kWindowSize);
            if (windowCount > 1) {
                return CallInWindows2<TReturn>(windowIndex, bank, fn);
            }
            switch (windowIndex) {
                case 1:  return CallInWindow<TReturn>(window2Control, bank, fn);
                case 2:  return CallInWindow<TReturn>(window3Control, bank, fn);
                default: return CallInWindow<TReturn>(window1Control, bank, fn);
            }
        }
    };
};

template <> struct VRC1::bank_layout<VRC1::fixed_bank_tag> {
    static constexpr bool always_mapped = true;
};

template <typename R, typename... A>
struct VRC1::function_traits<R (*)(A...)> {
    using return_type = R;
};

template <typename TReturn, typename TFunc>
fixed TReturn VRC1::Long(TFunc fn, const u8 window) {
#ifdef TARGET_NES
    switch (window) {
        case 1:  return Detail::CallInWindow<TReturn>(window2Control, 1, fn);
        case 2:  return Detail::CallInWindow<TReturn>(window3Control, 2, fn);
        default: return Detail::CallInWindow<TReturn>(window1Control, 0, fn);
    }
#else
    return fn();
#endif
}

template <auto Fn, typename... Args>
fixed auto VRC1::Call(Args &&...args) -> typename function_traits<decltype(Fn)>::return_type {
    using TReturn = typename function_traits<decltype(Fn)>::return_type;
#ifdef TARGET_NES
    using L = typename bank_of<Fn>::layout;
    if constexpr (L::always_mapped) {
        return Fn(std::forward<Args>(args)...);
    } else {
        return Detail::CallInSection<TReturn>(
            L::section(),
            [&]() -> TReturn { return Fn(std::forward<Args>(args)...); });
    }
#else
    return Fn(std::forward<Args>(args)...);
#endif
}

template <auto Fn, typename Block>
fixed auto VRC1::CallBlock(Block &&block) -> decltype(block()) {
#ifdef TARGET_NES
    using L = typename bank_of<Fn>::layout;
    if constexpr (L::always_mapped) {
        return block();
    } else {
        return Detail::CallInSection<decltype(block())>(
            L::section(), std::forward<Block>(block));
    }
#else
    return block();
#endif
}

/**
 * @brief Declares a tagged, out-of-line C++ function and registers it with
 *        VRC1::bank_of<>, so VRC1::Call/VRC1::CallBlock can resolve its bank
 *        later.
 *
 * section_name is whatever literal the CALLER's own linker script uses --
 * this library never picks or enforces a naming convention on it. tag is a
 * separate, plain-identifier-safe token used only to key bank_of<>/
 * bank_layout<Tag> (tag##_tag), kept apart from section_name because most
 * real section-name strings (leading dots, slashes) aren't valid identifier
 * characters. The VRC1_BANKED -> VRC1_BANKED_IMPL indirection is load-bearing:
 * it forces section_name/tag to be macro-expanded to their final literal text
 * before # or ## ever sees them, so a #define'd section_name/tag (see
 * VRC1_BANKED_EXTERN below) stringizes/pastes correctly instead of pasting
 * the macro's own name. Named VRC1_BANKED rather than plain BANKED
 * specifically so both mapper headers can be seen by the same translation
 * unit without a macro-redefinition clash -- mirrors mmc3.hpp's own
 * MMC3_BANKED naming for exactly the same reason.
 */
#define VRC1_BANKED(section_name, tag, ret, name, ...) \
    VRC1_BANKED_IMPL(section_name, tag, ret, name, __VA_ARGS__)
#define VRC1_BANKED_IMPL(section_name, tag, ret, name, ...)                      \
    CREATE_SEGMENT_KEYWORD(section_name) ret name(__VA_ARGS__);                 \
    template <> struct VRC1::bank_of<static_cast<ret (*)(__VA_ARGS__)>(&name)> { \
        using layout = VRC1::bank_layout<tag##_tag>;                            \
    }

/**
 * @brief Registers a function with VRC1::bank_of<> without declaring/placing it.
 *
 * For functions this project's own C++ codegen never emits -- hand-written
 * assembly, or a third-party engine assembled separately (FamiStudio; see
 * BANKED_CALL_THEORY.txt). Only performs the bank_of<> registration half;
 * actual placement is controlled by whatever assembled/linked that symbol,
 * and has to be kept in sync with section_name by hand (or a shared build
 * variable).
 *
 * The declaration carries its own `extern "C"` rather than relying on a
 * caller-supplied `extern "C" { ... }` block: every real user of this macro
 * (hand-written asm, ca65-assembled third-party code) exposes an unmangled,
 * C-style symbol name, so this is the correct default, not a shortcut --
 * and it has to be self-contained regardless, since the bank_of<>
 * specialization right below it is a template, which requires C++ linkage
 * and therefore can never itself sit inside an `extern "C"` block.
 */
#define VRC1_BANKED_EXTERN(section_name, tag, ret, name, ...) \
    VRC1_BANKED_EXTERN_IMPL(section_name, tag, ret, name, __VA_ARGS__)
#define VRC1_BANKED_EXTERN_IMPL(section_name, tag, ret, name, ...)               \
    extern "C" ret name(__VA_ARGS__);                                           \
    template <> struct VRC1::bank_of<static_cast<ret (*)(__VA_ARGS__)>(&name)> { \
        using layout = VRC1::bank_layout<tag##_tag>;                            \
    }
