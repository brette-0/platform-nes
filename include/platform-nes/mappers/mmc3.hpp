/**
 * @file mmc3.hpp
 * @brief MMC3 mapper (mapper 4): PRG/CHR bank switching, scanline IRQ, and
 *        the ::fixed / ::cartmem / ::sysmem segment keywords.
 *
 * MMC3 hardware, as this module uses it (PRG mode 0, CHR mode 0 -- the only
 * combination this module supports; see mmc3.cpp's ::_reset):
 *
 *   $8000-$9FFF  8 KiB PRG-ROM, switchable (register R6, via $8000/$8001)
 *   $A000-$BFFF  8 KiB PRG-ROM, switchable (register R7, via $8000/$8001)
 *   $C000-$DFFF  8 KiB PRG-ROM, FIXED to the second-to-last physical bank
 *   $E000-$FFFF  8 KiB PRG-ROM, FIXED to the last physical bank
 *   $6000-$7FFF  up to 8 KiB PRG-RAM ("cartridge memory", see ::cartmem)
 *   $0000-$07FF  CHR-ROM, six windows: two 2 KiB (R0/R1), four 1 KiB (R2-R5)
 *
 * UNLIKE VRC1's three independently-selectable PRG windows, MMC3 in PRG
 * mode 0 only exposes TWO registers a project can point anywhere: R6
 * ($8000) and R7 ($A000). $C000-$DFFF is hardware-fixed to the second-to-
 * last bank regardless of any register write -- there is no third
 * switchable window, and this file never declares a "window3Control": no
 * register backs one. Its content also isn't stable across ROM growth (it
 * always shows whatever is CURRENTLY second-to-last, which shifts as more
 * banks are added), so unlike ::fixed, nothing should target it through the
 * farcall machinery below -- see ::MMC3::Detail::CallInSection's own comment.
 *
 * Every register on this chip except $A000 (mirroring) and $A001 (PRG-RAM
 * protect) is reached through the SAME two physical addresses ($8000
 * select, $8001 data) -- see ::MMC3::Register, the indexed analogue of
 * VRC1's ::wo_register.
 *
 * This module is a class (::MMC3), not a namespace: every member is static,
 * and the class itself is non-instantiable (all constructors are deleted).
 * The NES target has exactly one physical MMC3 chip on the cartridge, so
 * there is never a real object to construct there -- ::MMC3 is used purely
 * as a scoping/organisational device, the same as the namespace it replaces.
 * A class (rather than a namespace) leaves room for a future emulated MMC3
 * -- e.g. a desktop/console-backend cartridge model -- to hold genuine
 * per-instance state as an ordinary object, without colliding names with
 * this NES-side, state-free version.
 */
#pragma once

#include <intsh>
#include <utility>

#include <platform-nes/technology.hpp>
#include <platform-nes/types.hpp>

using namespace br0::intsh;

// Alternative nametable wiring (NES2.0 flags 6, bit 3 -- the "four-screen"
// bit; see headers/nes2.hpp's own comment on the same macro, which defines
// this default under TARGET_NES from the CMake -D flag). This file is also
// compiled off-NES, by every emu backend, where CMake may not always pass
// the define through -- so it needs its own fallback default here too, in
// the shared header rather than just mmc3.cpp, since every TU that sees
// ::mmc3's class layout (below) must agree on which members exist. Only
// value 1 has a real implementation (src/emu/mappers/mmc3.cpp's strong
// ::ppu::ReadNametable/::WriteNametable overrides) -- see the static_assert.
#ifndef ALTERNATIVE_NAMETABLE
#define ALTERNATIVE_NAMETABLE 0
#endif

static_assert(ALTERNATIVE_NAMETABLE == 0 || ALTERNATIVE_NAMETABLE == 1,
    "mmc3: ALTERNATIVE_NAMETABLE must be 0 (ordinary switchable 2-screen "
    "mirroring) or 1 (four-screen: +2KiB cartridge VRAM) -- other nonzero "
    "values are reserved for a genuinely different alternative-wiring "
    "scheme, not folded into 1's meaning, and none is implemented yet.");

/**
 * @brief Pins a function or variable into MMC3's fixed PRG-ROM bank
 *        ($E000-$FFFF).
 *
 * Same name and section as VRC1's ::fixed (mmc3-helper.ld routes
 * `.prg_rom_fixed` to the identical $E000-$FFFF address range VRC1 does),
 * since both chips hardwire that window the same way. Expands to nothing
 * off-NES.
 */
#define fixed CREATE_SEGMENT_KEYWORD(".prg_rom_fixed")

/**
 * @brief Pins a variable into MMC3's PRG-RAM ("cartridge memory",
 *        $6000-$7FFF).
 *
 * Explicit placement, the same way ::fixed pins code into $E000-$FFFF: use
 * for state that should live in cartridge RAM specifically (e.g. a battery-
 * backed save struct) rather than wherever ordinary .bss/.data happens to
 * land. Backed by `.cartmem`, NOLOAD (mmc3-helper.ld) -- runtime storage
 * only, nothing is loaded into it from the ROM file. Expands to nothing
 * off-NES.
 */
#define cartmem CREATE_SEGMENT_KEYWORD(".cartmem")

/**
 * @brief Pins a variable into the NES's own onboard system RAM, explicitly.
 *
 * Ordinary globals already land in system RAM via .bss/.data's default
 * placement; ::sysmem exists for state that specifically must NOT drift
 * into ::cartmem or any other region by accident -- an explicit statement
 * of intent, not a different physical destination than the untagged
 * default. Backed by `.sysmem`, NOLOAD (mmc3-helper.ld). Expands to nothing
 * off-NES.
 */
#define sysmem CREATE_SEGMENT_KEYWORD(".sysmem")

/**
 * @brief MMC3 mapper (mapper 4) scoping class: PRG/CHR bank registers,
 *        scanline IRQ control, mirroring/PRG-RAM registers, and the
 *        banked-call scaffolding. See this file's own header comment.
 *
 * Every member is `static`; the class itself is non-instantiable (all
 * constructors deleted) since the NES target never needs -- and must never
 * create -- an object of it.
 */
class mmc3 {
private:
#ifndef TARGET_NES
    /**
     * @brief Emu-side shadow of MMC3's CHR mode bit (bit 7 of $8000).
     *
     * The real NES side of this module never sets this bit -- ::Register's
     * hardware write path hardcodes ::Detail::kModeBits to 0 permanently (see
     * this file's header comment: "the only combination this module
     * supports"), so on an actual cartridge this is always mode 0. The emu
     * side tracks it as real, settable state anyway (via ::SetCHRMode) so a
     * desktop/console build can model a game that DOES flip it, without
     * requiring the NES-side restriction to be lifted first.
     *
     * true  = large (2 KiB, R0/R1) windows at the front ($0000-$0FFF),
     *         small (1 KiB, R2-R5) windows at the end ($1000-$1FFF) --
     *         hardware's mode 0, and this module's only NES-side mode.
     * false = inverted: small windows at the front, large windows at the end.
     */
    static bool shape;

    /// Current bank number for each CHR register (R0-R5), indexed by
    /// register index -- banks[0]/banks[1] are 2 KiB-granularity (R0/R1),
    /// banks[2]-banks[5] are 1 KiB-granularity (R2-R5). Updated by
    /// ::Register::poke_only whenever chr0Control-chr5Control are written,
    /// off-NES only (see ::Register's own comment). Six entries, one per CHR
    /// register -- window1Control/window2Control (R6/R7) don't participate:
    /// off-NES there's no bank-switched PRG concept (::Long/::Call/::CallBlock
    /// already degrade to plain calls there), so nothing ever reads a PRG
    /// entry back out.
    static u8 banks[6];

    /// Off-NES write path for ::Register<Index>::poke_only, Index 0-5 only
    /// (see ::Register's own comment) -- records the bank a CHR register was
    /// just written with into ::banks, with no hardware poke (there is no
    /// hardware here). Defined in the emu-side mmc3.cpp
    /// (src/emu/mappers/mmc3.cpp), not the NES-side one.
    static void NotifyCHRWrite(u8 index, u8 bank);
#endif

public:
#ifndef TARGET_NES
    /**
     * @brief Translates a tile's PPU pattern-table address (0x0000-0x1FFF)
     *        into a byte offset in the flat CHR-ROM image, resolving it
     *        through the currently-selected CHR banks and ::shape.
     *
     * This is a tile ADDRESS translator, not a tile fetch: it returns an
     * offset for the caller to index into CHR-ROM itself (e.g.
     * `patternTable[GetTileLMA(chr_base)]`), it does not read any bytes
     * itself.
     *
     * Library-internal, like ::NotifyCHRWrite: the only caller is this
     * module's own strong `ppu::ResolveTile` override
     * (src/emu/mappers/mmc3.cpp), which the emu PPU (src/emu/ppu.cpp) calls
     * unconditionally for every tile fetch -- see that function's own doc
     * comment (video.hpp) for the weak/strong relationship. Public rather
     * than private purely because its caller is a free function, not a
     * member of this class -- C++ access control has no "same translation
     * unit" exemption the way `poke_only`'s access to ::NotifyCHRWrite gets
     * for free (::Register is a *nested* class of ::mmc3, so it already has
     * member-level access). A game selects banks through the ordinary
     * ::SwitchCHRBank(chr0Control, ...) call sites (shared with the NES
     * build) and never calls this directly.
     */
    static u32 GetTileLMA(u16 tileVMA);

#if ALTERNATIVE_NAMETABLE == 1
    /**
     * @brief Backing storage for this module's strong ::ppu::ReadNametable/
     *        ::ppu::WriteNametable overrides (src/emu/mappers/mmc3.cpp) --
     *        the software stand-in for the extra 2 KiB VRAM chip a real
     *        four-screen MMC3 board carries. Sized and allocated by the
     *        strong ::ppu::InitCartVRAM override, once, from
     *        ::emu::InitMemory.
     *
     * Only declared under ALTERNATIVE_NAMETABLE == 1: a board without this
     * wiring never routes anywhere but the console's own ::VideoRAM (the
     * weak ::ppu::ReadNametable/::WriteNametable defaults already do that),
     * so it has nothing to back. Public for the same free-function reason as
     * ::GetTileLMA, above -- not part of this class's intended user-facing
     * API.
     */
    static u8* cartVRAM;

    /// Byte length of one nametable row (::video::nametable_row_bytes(),
    /// NOT ::VideoRAM's own allocated size -- see ::ppu::InitCartVRAM's own
    /// doc comment, video.hpp, for why those can differ): everything below
    /// this logical offset answers from ::VideoRAM, everything at or above
    /// it answers from ::cartVRAM (offset by this same amount). Set
    /// alongside ::cartVRAM by ::ppu::InitCartVRAM.
    static unsigned cartVRAMRowBytes;
#endif

    /**
     * @brief Sets the emu-side CHR mode bit (see ::shape).
     *
     * Off-NES only: the real NES side of this module never exposes a way to
     * change this (it hardcodes mode 0, see ::Detail::kModeBits), so this
     * exists purely for a desktop/console build that wants to model a game
     * which does flip it. Calling this on the NES target would have no
     * hardware counterpart to reflect the change, so it isn't declared
     * there.
     *
     * @param largeWindowsAtFront true selects hardware's mode 0 (this
     *                            module's own default and its NES side's
     *                            only mode); false inverts it.
     */
    static void SetCHRMode(bool largeWindowsAtFront);
#endif

    mmc3() = delete;
    mmc3(const mmc3 &) = delete;
    mmc3 &operator=(const mmc3 &) = delete;

    /**
     * @brief One of MMC3's eight indexed bank registers (R0-R7), reached
     *        through the shared $8000 (select) / $8001 (data) port pair.
     *
     * Unlike ::tech::wo_register (one dedicated hardware address per
     * instance), every Register<Index> shares the SAME two physical
     * addresses -- what makes an instance distinct is which index gets
     * latched into $8000 immediately before the $8001 write. Real hardware
     * has eight independent internal latches behind that one port pair, so
     * each instance still owns its own RAM shadow, and get()/::SHADOW/
     * save-restore all work exactly like ::tech::wo_register's.
     *
     * @tparam Index Register select value, 0-7 (R0-R7).
     */
    template <u8 Index>
    class Register {
        atomic u8 shadow_;
    public:
        static_assert(Index <= 7, "MMC3 only has registers R0-R7.");

        Register() = default;                                    ///< trivial: instance lives in .bss
        Register(const Register &o) : shadow_(o.shadow_) {}     ///< snapshot, no poke

        u8   get() const     { return shadow_; }                          ///< read  = shadow
        void set(const u8 v) { shadow_ = v; poke_only(v); }               ///< write = shadow + hardware
        /**
         * @brief Write hardware only, no shadow update.
         *
         * On NES: the real $8000/$8001 port-pair write. Off-NES: there is no
         * hardware, so this instead updates the emu's own bank-tracking state
         * (::NotifyCHRWrite) for CHR registers (Index 0-5) so ::GetTileLMA can
         * resolve tile fetches correctly; PRG registers (Index 6/7) are a
         * no-op off-NES, since off-NES there's no bank-switched PRG concept
         * to track (::Long/::Call/::CallBlock already degrade to plain calls
         * there).
         */
        void poke_only(const u8 v) const {
#ifdef TARGET_NES
            tech::poke(0x8000, Detail::kModeBits | Index);
            tech::poke(0x8001, v);
#else
            if constexpr (Index <= 5) NotifyCHRWrite(Index, v);
#endif
        }

        operator u8() const { return shadow_; }
        Register &operator=(const u8 v)          { set(v);         return *this; }
        Register &operator=(const Register &o)   { set(o.shadow_); return *this; } ///< restore path
    };

    /**
     * @brief MMC3's two register-controlled PRG-select windows.
     *
     * Not `const`: same reasoning as VRC1's window1Control etc. -- a write-only
     * register that can never be written defeats the point. Defined (not just
     * declared) in mmc3.cpp, where the default-bank boot-time init also lives.
     * There is no window3Control -- see this file's header comment.
     */
    static Register<6> window1Control; ///< PRG-select R6, window 1 ($8000-$9FFF). See above.
    static Register<7> window2Control; ///< PRG-select R7, window 2 ($A000-$BFFF). See ::window1Control.

    /**
     * @brief MMC3's six CHR-select registers (R0-R5).
     *
     * chr0Control/chr1Control select 2 KiB windows (PPU $0000/$0800 under CHR
     * mode 0), but -- like every CHR register on this chip -- the value
     * written still counts banks in 1 KiB units; the low bit is ignored by
     * hardware (an odd bank can't be selected, since it wouldn't be 2 KiB-
     * aligned), though nothing here masks it away -- the shadow simply holds
     * whatever was written, so a consumer resolving a tile address (e.g.
     * ::GetTileLMA) must mask it off itself, not just multiply by 2 KiB.
     * chr2Control-chr5Control select 1 KiB banks (PPU $1000/$1400/$1800/
     * $1C00), full 8-bit range (up to 256 KiB CHR-ROM). Named chrNControl to
     * match VRC1's chr0Control/chr1Control convention as closely as this
     * chip's extra granularity allows.
     */
    static Register<0> chr0Control; ///< CHR-select R0, 2 KiB @ PPU $0000.
    static Register<1> chr1Control; ///< CHR-select R1, 2 KiB @ PPU $0800.
    static Register<2> chr2Control; ///< CHR-select R2, 1 KiB @ PPU $1000.
    static Register<3> chr3Control; ///< CHR-select R3, 1 KiB @ PPU $1400.
    static Register<4> chr4Control; ///< CHR-select R4, 1 KiB @ PPU $1800.
    static Register<5> chr5Control; ///< CHR-select R5, 1 KiB @ PPU $1C00.

    /**
     * @brief Selects nametable mirroring ($A000).
     *
     * On NES: a direct $A000 write (bit 0: 0 = vertical, 1 = horizontal --
     * opposite polarity from the iNES header's own mirroring bit, a
     * well-known MMC3 gotcha, not a typo). Off-NES: no hardware to write, so
     * this instead writes the emu PPU's own library-visible ::mirroring
     * global (video.hpp) directly, for the emu side to consult later.
     *
     * @param horizontal false selects vertical mirroring (side-by-side
     *                   nametables); true selects horizontal.
     */
    static void SetMirroring(bool horizontal);

    /**
     * @brief PRG-RAM enable/write-protect ($A001). Bit 6 = RAM enable, bit 7 =
     *        write-protect. mmc3.cpp's ::_reset enables RAM, writable, at boot.
     */
    static tech::wo_register<0xa001> prgRamProtect;

    /**
     * @brief Scanline IRQ reload latch ($C000) -- the value the counter is set
     *        to whenever it reloads. See ::ScheduleScanlineIRQ.
     */
    static tech::wo_register<0xc000> irqLatch;

    /**
     * @brief Writes @p ctx into an MMC3 bank register, banking in that
     *        window/CHR slice.
     *
     * Thin, named wrapper over ::Register's write path, mirroring VRC1's
     * ::SwitchBank shape exactly. @p reg is taken by reference for the same
     * reason ::SwitchBank documents: by value, the hardware poke would still
     * happen, but the shadow update would be lost.
     *
     * Not used by ::_reset (mmc3.cpp): that runs before .bss has been zeroed,
     * so window1Control/window2Control's shadows aren't valid to write yet --
     * see ::_reset's own comment for why it pokes hardware directly instead.
     */
    template <u8 Index>
    static constexpr void SwitchBank(Register<Index> &reg, u8 ctx) {
        reg = ctx;
    }

    /**
     * @brief Selects which CHR-ROM bank appears in one of MMC3's six CHR
     *        windows.
     *
     * A named alias for ::SwitchBank restricted to the CHR registers
     * (chr0Control-chr5Control), matching VRC1's separate ::SwitchCHRBank entry
     * point -- MMC3's hardware doesn't actually need a different write path
     * (unlike VRC1's shared chrHighBits read-modify-write), but the distinct
     * name keeps PRG and CHR bank switches visually distinguishable at the call
     * site, same as on VRC1.
     *
     * @param reg  chr0Control through chr5Control; any other instantiation is a
     *             compile error.
     * @param bank Target bank, full 8-bit range (0-255) for every CHR
     *             register, all in 1 KiB units -- for chr0Control/chr1Control
     *             specifically, an odd value's low bit is ignored by hardware
     *             (see ::chr0Control's own comment), so only even values
     *             actually select a distinct 2 KiB window.
     */
    template <u8 Index>
    static constexpr void SwitchCHRBank(Register<Index> &reg, u8 bank) {
        static_assert(Index <= 5, "SwitchCHRBank only valid for chr0Control-chr5Control (R0-R5).");
        reg = bank;
    }

    /**
     * @brief Arms MMC3's scanline IRQ counter to fire after @p scanline more
     *        PPU A12 rising edges (in practice, roughly @p scanline scanlines
     *        with 8x8 sprites/background rendering enabled).
     *
     * Sets the reload latch ($C000), forces an immediate reload on the next
     * clock ($C001), and enables IRQ generation ($E001). Does not by itself
     * acknowledge any IRQ still pending from a previous split -- call
     * ::AcknowledgeScanlineIRQ first if one might be. Pairs naturally with this
     * project's ::IRQ (interrupts.hpp): the user-written handler placed there
     * knows which split is due, does its work, acknowledges, and schedules the
     * next one.
     *
     * @param scanline Reload value for the countdown; 0 fires on the very next
     *                 qualifying PPU address change.
     */
    static void ScheduleScanlineIRQ(u8 scanline);

    /**
     * @brief Disables further scanline IRQs and clears any currently pending
     *        one ($E000).
     *
     * Purely the acknowledge/disable half -- it does NOT re-enable or re-arm
     * the counter. Call ::ScheduleScanlineIRQ afterward to schedule the next
     * split; keeping the two separate means acknowledging a handler that
     * genuinely has nothing more to schedule this frame doesn't silently leave
     * the counter armed.
     */
    static void AcknowledgeScanlineIRQ();

    /*
     * BANKED CALL scaffolding, mirroring vrc1.hpp's own (see
     * BANKED_CALL_THEORY.txt) as closely as MMC3's two-window hardware allows.
     * Nested under MMC3:: (unlike VRC1's global versions) per this file's own
     * convention -- use ::MMC3_BANKED / ::MMC3_BANKED_EXTERN (below, outside
     * the class) to register a tagged function; they qualify MMC3::bank_of /
     * MMC3::bank_layout for you.
     */

    /// One physical bank/segment's location in the linked ROM image. Same
    /// shape and same reasoning as VRC1's section_t: rom_address is
    /// LOADADDR()-derived, not the CPU-visible VMA, since every bank that ever
    /// aliases onto a switchable window shares one VMA.
    struct section_t {
        u32 rom_address;
        u32 size;
    };

    /// Hardware facts for one tag/domain -- same contract as VRC1's
    /// bank_layout<Tag>: primary template undefined (an un-tagged Tag is a
    /// compile error), a non-fixed specialization provides
    /// `static section_t section()`. See vrc1.hpp's own bank_layout<Tag> doc
    /// comment for the constexpr-vs-runtime-section() cost tradeoff, which
    /// applies identically here.
    template <typename Tag> struct bank_layout;

    /// Key for this file's own always-mapped fixed bank ($E000-$FFFF, see
    /// ::fixed). Named fixed_bank_tag, not fixed_tag, for the same reason
    /// vrc1.hpp's own fixed_bank_tag is: `fixed` is itself a macro in this
    /// file, so it can never be used as a bare tag## token.
    struct fixed_bank_tag {};

    /// Which bank_layout<Tag> a specific tagged function was registered under.
    /// One specialization per MMC3_BANKED()/MMC3_BANKED_EXTERN() call site.
    /// Primary template deliberately undefined, same reasoning as
    /// MMC3::bank_layout<Tag>.
    template <auto Fn> struct bank_of;

    /// Return-type extraction for a plain function pointer, used by MMC3::Call.
    template <typename T> struct function_traits;

    /**
     * @brief Calls @p fn with window1Control or window2Control temporarily
     *        re-banked, then restores whatever was banked there before
     *        returning.
     *
     * Manual, non-tag-based counterpart to ::Call -- mirrors VRC1's ::Long
     * exactly, with one fewer valid window (0 or 1 only; MMC3 has no third
     * register-controlled window, see this file's header comment).
     *
     * @tparam TReturn Result type of the call; specify explicitly, e.g.
     *                 `Long<int>(...)`.
     * @param fn     Callable to invoke once the selected window is banked in.
     * @param window Which window to switch: 0 (window1Control/$8000) or 1
     *               (window2Control/$A000); default 0. Off-NES this parameter
     *               is ignored.
     */
    template <typename TReturn, typename TFunc>
    static fixed TReturn Long(TFunc fn, u8 window = 0);

    /**
     * @brief Calls a specific, compile-time-known MMC3_BANKED() function,
     *        resolving its bank from MMC3::bank_of<Fn> automatically.
     *
     * Identical shape and behaviour to VRC1's ::Call.
     *
     * @tparam Fn Address of an MMC3_BANKED()/MMC3_BANKED_EXTERN()-tagged
     *            function -- supplied explicitly, e.g. `Call<LoadChunk>(id)`.
     */
    template <auto Fn, typename... Args>
    static fixed auto Call(Args &&...args) -> typename function_traits<decltype(Fn)>::return_type;

    /**
     * @brief Runs an arbitrary block under Fn's resolved window, instead of
     *        calling Fn itself. Identical shape and behaviour to VRC1's
     *        ::CallBlock.
     */
    template <auto Fn, typename Block>
    static fixed auto CallBlock(Block &&block) -> decltype(block());

private:
    /**
     * @brief Implementation details not part of MMC3's public interface.
     *        Private nested class purely as a namespacing device -- a class
     *        cannot nest a plain `namespace`, so this plays the same role
     *        the free-standing `namespace detail` played before.
     */
    struct Detail {
        /**
         * @brief Bit 6 (PRG mode) / bit 7 (CHR A12 invert) of every $8000 write.
         *
         * $8000 packs the register-select index (bits 0-2) together with these
         * two latched mode bits in the SAME byte, so selecting a different
         * index without re-asserting the last mode bits would silently clear
         * whichever one wasn't also specified again. This module fixes both
         * bits at 0 permanently (PRG mode 0, CHR non-inverted -- see this
         * file's header comment) and never exposes a way to change them, so
         * every $8000 write below is simply the bare register index; there is
         * no runtime state to track. If a future revision needs to flip either
         * bit, THIS is the one place that assumption lives.
         */
        static constexpr u8 kModeBits = 0;

        /// Windows 1/2's shared geometry: WINDOW_SIZE bytes each, starting at
        /// WINDOW_BASE ($8000, window1Control's own base). Same constants as
        /// VRC1's -- both chips use 8 KiB windows starting at $8000 -- but only
        /// windowIndex 0/1 are ever valid here (see ::CallInSection's own
        /// comment); MMC3 has no windowIndex-2 register to switch to.
        static constexpr u16 kWindowBase = 0x8000;
        static constexpr u16 kWindowSize = 0x2000;

        /**
         * @brief Implementation of ::MMC3::Long / ::CallInSection once the
         *        target register is resolved. Not for direct use.
         *
         * Same shape, and the same LOAD-BEARING [[gnu::noinline]] reasoning, as
         * vrc1_detail::CallInWindow: `fixed` only places an out-of-line symbol,
         * and inlining this into a caller that itself lives in the window being
         * switched away from is a real, reproduced crash on VRC1 (see
         * BANKED_CALL_THEORY.txt) -- the same failure mode applies here
         * unchanged, since it's a property of "switch a bank out from under
         * code that keeps running in that bank," not anything VRC1-specific.
         */
        template <typename TReturn, u8 Index, typename TFunc>
        [[gnu::noinline]] static fixed TReturn CallInWindow(Register<Index> &reg, u8 bank, TFunc fn) {
            SHADOW(reg) {
                SwitchBank(reg, bank);
                return fn();
            }
            __builtin_unreachable(); // SHADOW's scope always runs its body
                                     // exactly once and returns.
        }

        /**
         * @brief Resolves a section_t to whichever of window1Control/
         *        window2Control MMC3 needs, then runs @p fn.
         *
         * Same WINDOW/BANK derivation as vrc1_detail::CallInSection: WINDOW
         * comes from the low 16 bits of rom_address (truncated exactly like a
         * real JSR operand would be), BANK defaults to WINDOW unless
         * bank_layout<Tag>::section() hand-encodes an explicit bank in
         * rom_address's high bits.
         *
         * ONLY windowIndex 0 or 1 are valid ($8000 or $A000) -- MMC3 exposes no
         * register for $C000-$DFFF (this file's header comment), so a
         * bank_layout<Tag>::section() whose address falls at $C000 or above is
         * a caller error, not something this module can route correctly; it is
         * NOT range-checked here (matching vrc1_detail::CallInSection, which
         * also trusts its caller's hand-entered addresses) and falls through to
         * window1Control's case, silently mis-banking. Verify any hand-entered
         * section() address stays below $C000 before relying on it, the same
         * way vrc1.ld's own ASSERTs cross-check its hand-entered bank numbers.
         *
         * There is no CallInWindows2 equivalent (VRC1's oversized-domain,
         * Phase 4 case): MMC3's total register-controlled PRG space is only 16
         * KiB across these two windows, half of VRC1's 24 KiB across three, so
         * a domain spanning both windows at once is unsupported here -- add it
         * the same way vrc1.hpp's CallInWindows2 does if a real need arises.
         */
        template <typename TReturn, typename TFunc>
        [[gnu::noinline]] static fixed TReturn CallInSection(const section_t &section, TFunc fn) {
            const u16 vma = static_cast<u16>(section.rom_address);
            const u8 windowIndex = static_cast<u8>((vma - kWindowBase) / kWindowSize);
            const u8 explicitBank = static_cast<u8>(section.rom_address >> 16);
            const u8 bank = explicitBank != 0 ? explicitBank : windowIndex;
            switch (windowIndex) {
                case 1:  return CallInWindow<TReturn>(window2Control, bank, fn);
                default: return CallInWindow<TReturn>(window1Control, bank, fn);
            }
        }
    };
};

template <> struct mmc3::bank_layout<mmc3::fixed_bank_tag> {
    static constexpr bool always_mapped = true;
};

template <typename R, typename... A>
struct mmc3::function_traits<R (*)(A...)> {
    using return_type = R;
};

template <typename TReturn, typename TFunc>
fixed TReturn mmc3::Long(TFunc fn, const u8 window) {
#ifdef TARGET_NES
    switch (window) {
        case 1:  return Detail::CallInWindow<TReturn>(window2Control, 1, fn);
        default: return Detail::CallInWindow<TReturn>(window1Control, 0, fn);
    }
#else
    return fn();
#endif
}

template <auto Fn, typename... Args>
fixed auto mmc3::Call(Args &&...args) -> typename function_traits<decltype(Fn)>::return_type {
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
fixed auto mmc3::CallBlock(Block &&block) -> decltype(block()) {
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
 *        MMC3::bank_of<>, so MMC3::Call/MMC3::CallBlock can resolve its
 *        bank later.
 *
 * Same VRC1_BANKED -> VRC1_BANKED_IMPL macro-expansion-order reasoning as
 * VRC1's ::VRC1_BANKED (see its own comment): forces section_name/tag to
 * their final literal text before # or ## ever sees them. Named MMC3_BANKED
 * rather than plain BANKED specifically so both mapper headers can be seen
 * by the same translation unit without a macro-redefinition clash -- a bare
 * `BANKED` name would collide the moment both this header and vrc1.hpp are
 * included together, so each mapper gets its own prefixed macro
 * (MMC3_BANKED / VRC1_BANKED) expanding to its own qualified class
 * (MMC3:: / VRC1::) instead.
 */
#define MMC3_BANKED(section_name, tag, ret, name, ...) \
    MMC3_BANKED_IMPL(section_name, tag, ret, name, __VA_ARGS__)
#define MMC3_BANKED_IMPL(section_name, tag, ret, name, ...)                      \
    CREATE_SEGMENT_KEYWORD(section_name) ret name(__VA_ARGS__);                 \
    template <> struct MMC3::bank_of<static_cast<ret (*)(__VA_ARGS__)>(&name)> { \
        using layout = MMC3::bank_layout<tag##_tag>;                            \
    }

/**
 * @brief Registers a function with MMC3::bank_of<> without declaring/
 *        placing it -- for hand-written assembly or a separately-assembled
 *        third-party engine. See VRC1's ::BANKED_EXTERN for the full
 *        reasoning; identical here except for the MMC3:: qualification.
 */
#define MMC3_BANKED_EXTERN(section_name, tag, ret, name, ...) \
    MMC3_BANKED_EXTERN_IMPL(section_name, tag, ret, name, __VA_ARGS__)
#define MMC3_BANKED_EXTERN_IMPL(section_name, tag, ret, name, ...)               \
    extern "C" ret name(__VA_ARGS__);                                           \
    template <> struct MMC3::bank_of<static_cast<ret (*)(__VA_ARGS__)>(&name)> { \
        using layout = MMC3::bank_layout<tag##_tag>;                            \
    }
