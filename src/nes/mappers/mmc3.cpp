#include <platform-nes/mappers/mmc3.hpp>

mmc3::Register<6> mmc3::window1Control;
mmc3::Register<7> mmc3::window2Control;

mmc3::Register<0> mmc3::chr0Control;
mmc3::Register<1> mmc3::chr1Control;
mmc3::Register<2> mmc3::chr2Control;
mmc3::Register<3> mmc3::chr3Control;
mmc3::Register<4> mmc3::chr4Control;
mmc3::Register<5> mmc3::chr5Control;

tech::wo_register<0xa001> mmc3::prgRamProtect;
tech::wo_register<0xc000> mmc3::irqLatch;

void mmc3::SetMirroring(const bool horizontal) {
    tech::poke(0xa000, horizontal ? 1 : 0);
}

// @p position is off-NES only (see this function's own doc comment,
// mmc3.hpp) -- real hardware's scanline counter has no pixel-column concept
// for it to mean, so it's simply unused here.
void mmc3::ScheduleScanlineIRQ(const u8 scanline, vec2<u16>) {
    irqLatch = scanline; // $C000: reload value.
    tech::poke(0xc001, 0);     // $C001: force reload at next clock (value ignored).
    tech::poke(0xe001, 0);     // $E001: enable (value ignored).
}

void mmc3::AcknowledgeScanlineIRQ() {
    tech::poke(0xe000, 0); // $E000: disable + acknowledge pending IRQ (value ignored).
}

extern "C" void _start();

/**
 * @brief The two physical PRG banks that make $8000-$BFFF continuous with
 *        the rest of the always-resident image, defined by mmc3-helper.ld.
 *
 * NOT 0 and 1 in general: mmc3-helper.ld anchors the resident image to the
 * TOP of the ROM file (banks N-1-windows .. N-1) so that MMC3's two
 * hardwired windows -- last bank at $E000, second-to-last at $C000 -- always
 * show linked content no matter how big PRG-ROM is. Anything below that is
 * spare/banked space. 0/1 is simply what those two work out to at the 4-bank
 * minimum, which is the only size this project built while these were
 * hardcoded literals here.
 *
 * Declared as arrays and used via their ADDRESS: a linker-script symbol has
 * no storage to load a value out of -- `__mmc3_boot_bank_window1 = 60;` in
 * the script means the symbol's ADDRESS is 60 -- so taking the address and
 * truncating to u8 is what actually reads the number. Same trick llvm-mos's
 * own platform code uses for __prg_rom_size-style layout symbols; a plain
 * `extern u8` here would instead fetch whatever byte lives at address 60.
 */
extern "C" const char __mmc3_boot_bank_window1[];
extern "C" const char __mmc3_boot_bank_window2[];

/// @see __mmc3_boot_bank_window1 -- address-as-value, narrowed to the 8 bits
/// MMC3's bank registers take (only the low 6 are decoded by the chip; the
/// linker script's own ASSERT on __prg_rom_size <= 512 is what keeps the
/// number inside them). always_inline, not merely `static`: ::_reset calls
/// this before R6/R7 are established, so it must not become a real call into
/// a switchable bank -- and it is not marked ::FIXED, since inlining leaves
/// nothing to place. __UINTPTR_TYPE__ rather than <cstdint>'s uintptr_t: this
/// file includes no standard headers, and the builtin is always available.
[[gnu::always_inline]] static inline u8 BootBank(const char *symbol) {
    return static_cast<u8>(reinterpret_cast<__UINTPTR_TYPE__>(symbol));
}

/**
 * @brief Cold-boot init: silence the scanline IRQ unit, point R6/R7 at the
 *        flat image's first two banks, enable PRG-RAM, then fall through
 *        to crt0's _start.
 *
 * Runs BEFORE .bss is zeroed (same constraint as VRC1's ::_reset, see its
 * own comment in vrc1.cpp): raw tech::poke() calls only, no MMC3::Register
 * shadow objects, and no calls to ordinary (non-::FIXED) functions like
 * ::AcknowledgeScanlineIRQ -- window1Control/window2Control aren't
 * established yet at this point, so anything not pinned to this fixed bank
 * could resolve into whatever switchable bank happens to be powered-up
 * mapped, not necessarily the right one. Real MMC3 hardware powers up with
 * R0-R7, mirroring, PRG-RAM, AND the scanline-IRQ unit's
 * $C000/$C001/$E000/$E001 all undefined (nesdev.org/wiki/MMC3: "the values
 * in R6, R7, and $8000 are unspecified at power on"; the IRQ unit is a
 * separate functional block from R0-R7/$A000/$A001 and gets no special
 * treatment on real silicon). Nothing here can be skipped as "probably
 * already correct."
 *
 * The $E000 write is the safety-critical one, not just tidiness: it's the
 * one omission that could crash the game outright rather than just
 * misdraw a frame. If the IRQ-enable latch happens to already be set
 * (power-on state is genuinely undefined, and unlike R6/R7/$A001 -- which
 * this function reasserts on every single entry, so a soft reset can't
 * leave them stale -- an untouched $E000 latch survives however this
 * function left it last), the scanline counter can fire well before any
 * application code ever calls ::ScheduleScanlineIRQ. It doesn't even need
 * rendering on to do that: A12 (which clocks the counter) toggles on
 * ordinary $2006 writes too, and early boot is full of exactly those
 * (nametable/palette/attribute setup). The CPU's own interrupt-disable
 * flag -- set by the reset sequence itself, and still set through all of
 * this -- holds any such request pending instead of servicing it
 * immediately, so it surfaces later, right when ::EnableInterrupts
 * finally runs: a crash that looks intermittent/hardware-dependent rather
 * than consistent, because it tracks whatever residual state the counter/
 * latch happened to power up (or survive a soft reset) in, rather than
 * anything the source code visibly does differently run to run. Writing
 * $E000 unconditionally, first, before anything else, forecloses that
 * regardless of what state it finds: per the wiki it disables IRQ
 * generation outright (the counter keeps ticking in the background,
 * harmlessly, until the first real ::ScheduleScanlineIRQ re-arms it) and
 * acknowledges whatever was pending, so there is nothing left to fire once
 * ::EnableInterrupts runs later. $C000/$C001 don't need a matching
 * pre-seed: ::ScheduleScanlineIRQ already writes both before ever
 * re-enabling via $E001, so the counter's value is fully deterministic
 * again well before it can matter.
 *
 * R0-R5 (CHR banks) and $A000 (mirroring) are deliberately NOT touched
 * here, same as VRC1's ::_reset leaves its own CHR windows unset: those
 * are per-game choices, not a mapper-level bootstrap fact the way "make
 * PRG a flat image" or "don't let a stale IRQ fire into unready code" are,
 * and getting them wrong is cosmetic (wrong pattern-table content/
 * nametable layout while rendering is still off) rather than a crash. This
 * project's demo sets CHR banks explicitly in its own RESET body (before
 * enabling rendering) -- but currently leaves mirroring unset, relying on
 * whatever the mapper happens to power up with; that's a real gap too if
 * it needs to be reliable, just not a crashing one, and not this
 * function's call to make since it doesn't know what layout any given game
 * wants.
 *
 * With no explicit MMC3_BANKED() domain in use, this reconstructs one flat,
 * contiguous $8000-$FFFF image out of the ROM's top four banks, at ANY
 * PRG-ROM size from 32 KiB to MMC3's own 512 KiB maximum: R6/R7 get the
 * resident image's first two banks (__mmc3_boot_bank_window1/2, from
 * mmc3-helper.ld -- N-4 and N-3, not 0 and 1, once the ROM is bigger than
 * the resident image), and the remaining two show up at $C000-$DFFF/
 * $E000-$FFFF automatically (second-to-last/last, PRG mode 0 hardware fact)
 * with no register write needed for either. That works size-independently
 * only because the linker script anchors the resident image to the top of
 * the ROM file -- see mmc3-helper.ld's own header comment.
 */
extern "C" FIXED void _reset() {
    tech::poke(0xe000, 0);    // IRQ: disable generation + acknowledge any pending/residual IRQ.
    tech::poke(0xa001, 0x80); // PRG-RAM: enable, write-permitted.
    // R6/R7 -> the resident image's own first two banks ($8000-$9FFF,
    // $A000-$BFFF). See ::__mmc3_boot_bank_window1: these are the ROM's top
    // banks, not 0/1, on anything larger than the 32 KiB minimum.
    tech::poke(0x8000, 6); tech::poke(0x8001, BootBank(__mmc3_boot_bank_window1));
    tech::poke(0x8000, 7); tech::poke(0x8001, BootBank(__mmc3_boot_bank_window2));
    _start();
}

/**
 * @brief Re-syncs window1Control/window2Control's RAM shadows to match
 *        what ::_reset already poked into hardware.
 *
 * Same reasoning and same ordering constraint as VRC1's SyncBankShadows:
 * runs as a global constructor, after crt0's .bss zeroing, which is what
 * makes writing through ::SwitchBank safe here but not in ::_reset.
 */
__attribute__((constructor(101)))
static void SyncBankShadows() {
    mmc3::SwitchBank(mmc3::window1Control, BootBank(__mmc3_boot_bank_window1));
    mmc3::SwitchBank(mmc3::window2Control, BootBank(__mmc3_boot_bank_window2));
}
