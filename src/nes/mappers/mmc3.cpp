#include <platform-nes/mappers/mmc3.hpp>

namespace mmc3 {

mmc3_register<6> window1Control;
mmc3_register<7> window2Control;

mmc3_register<0> chr0Control;
mmc3_register<1> chr1Control;
mmc3_register<2> chr2Control;
mmc3_register<3> chr3Control;
mmc3_register<4> chr4Control;
mmc3_register<5> chr5Control;

wo_register<0xa000> mirroring;
wo_register<0xa001> prgRamProtect;
wo_register<0xc000> irqLatch;

void ScheduleScanlineIRQ(const u8 scanline) {
    irqLatch = scanline; // $C000: reload value.
    poke(0xc001, 0);     // $C001: force reload at next clock (value ignored).
    poke(0xe001, 0);     // $E001: enable (value ignored).
}

void AcknowledgeScanlineIRQ() {
    poke(0xe000, 0); // $E000: disable + acknowledge pending IRQ (value ignored).
}

} // namespace mmc3

extern "C" void _start();

/**
 * @brief Cold-boot init: silence the scanline IRQ unit, point R6/R7 at the
 *        flat image's first two banks, enable PRG-RAM, then fall through
 *        to crt0's _start.
 *
 * Runs BEFORE .bss is zeroed (same constraint as VRC1's ::_reset, see its
 * own comment in vrc1.cpp): raw poke() calls only, no mmc3:: register
 * shadow objects, and no calls to ordinary (non-::fixed) functions like
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
 * At this module's assumed 32 KiB PRG-ROM (4 banks) with no explicit
 * .prg_rom_bankN tagging in use, this reconstructs one flat, contiguous
 * $8000-$FFFF image: bank 0 -> R6 ($8000-$9FFF), bank 1 -> R7
 * ($A000-$BFFF), and banks 2/3 show up at $C000-$DFFF/$E000-$FFFF
 * automatically (second-to-last/last, PRG mode 0 hardware fact) with no
 * register write needed for either -- see mmc3-helper.ld's own MEMORY
 * comment.
 */
extern "C" fixed void _reset() {
    poke(0xe000, 0);    // IRQ: disable generation + acknowledge any pending/residual IRQ.
    poke(0xa001, 0x80); // PRG-RAM: enable, write-permitted.
    poke(0x8000, 6); poke(0x8001, 0); // R6 -> bank 0 ($8000-$9FFF).
    poke(0x8000, 7); poke(0x8001, 1); // R7 -> bank 1 ($A000-$BFFF).
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
    mmc3::SwitchBank(mmc3::window1Control, 0);
    mmc3::SwitchBank(mmc3::window2Control, 1);
}
