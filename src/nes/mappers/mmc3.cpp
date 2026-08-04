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
 * @brief Cold-boot init: point R6/R7 at the flat image's first two banks,
 *        enable PRG-RAM, then fall through to crt0's _start.
 *
 * Runs BEFORE .bss is zeroed (same constraint as VRC1's ::_reset, see its
 * own comment in vrc1.cpp): raw poke() calls only, no mmc3:: register
 * shadow objects -- they aren't valid to touch yet. Real MMC3 hardware
 * powers up with R0-R7 and the mirroring/PRG-RAM registers all undefined,
 * so nothing here can be skipped as "probably already correct."
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
