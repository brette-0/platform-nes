#include <platform-nes/mappers/vrc1.hpp>

wo_register<0x8000> window1Control;
wo_register<0xa000> window2Control;
wo_register<0xc000> window3Control;

extern "C" void _start();

/**
 * @brief VRC1's actual reset-vector target (see vrc1.ld) -- pokes the
 *        default (NROM-equivalent) bank layout directly into hardware,
 *        then falls into crt0's real entry point.
 *
 * Real VRC1 hardware powers up with undefined PRG-select state, so at cold
 * boot the fixed bank ($E000-$FFFF) is the only window guaranteed correct
 * -- see the precondition note atop vrc1.ld. crt0's own entry point,
 * _start, lives in ordinary .text (the switchable region) along with the
 * rest of crt0's startup code that runs before main -- none of which is
 * safe to enter directly from reset, since the switchable windows could be
 * showing any physical bank at power-on. _reset runs first, entirely from
 * the fixed bank (::fixed), and only calls into _start once the
 * switchable windows have actually been set to what this script's layout
 * assumes is there.
 *
 * Pokes $8000/$A000/$C000 directly with ::poke rather than going through
 * ::SwitchBank / ::wo_register: this runs *before* crt0's .bss zeroing
 * (which happens inside _start, right after this falls through to it), and
 * window1Control/2/3 live in .bss. Writing their shadow now would just get
 * clobbered a moment later when .bss is zeroed -- see ::SyncBankShadows,
 * which re-does this through the normal shadowed path once that's no
 * longer a concern. ::poke is `always_inline`, so this is guaranteed to
 * stay inside _reset's own ::fixed-tagged code with no separate,
 * potentially-switchable-region subroutine call -- confirmed by inspecting
 * the compiled output; an earlier attempt to reuse ::SwitchBank here
 * looked correct in source but the compiler emitted its trivial one-line
 * body as an out-of-line call at -O0, which landed in the switchable
 * region and defeated the entire point of this function.
 *
 * An earlier version of this project used a plain global constructor for
 * all of this instead of a real _reset target. That doesn't work:
 * constructors run from inside crt0's own startup sequence, which is
 * itself ordinary .text -- i.e. exactly the code this function exists to
 * make safe to reach in the first place. By the time a constructor could
 * run, the damage (if any) is already done.
 *
 * window1=0 / window2=1 / window3=2 reproduces the flat NROM-equivalent
 * layout vrc1.ld already assumes: at the project's current 32 KiB PRG-ROM
 * (banks 0-3), that's every bank except the fixed one ($E000, hardwired by
 * the mapper to the last physical bank regardless of register state).
 */
extern "C" fixed void _reset() {
    poke(0x8000, 0);
    poke(0xa000, 1);
    poke(0xc000, 2);
    _start();
}

/**
 * @brief Re-syncs window1Control/2/3's RAM shadows to match what ::_reset
 *        already poked into hardware.
 *
 * Runs as an ordinary global constructor -- i.e. after crt0's .bss zeroing,
 * which is what makes writing through ::SwitchBank safe here but not in
 * ::_reset (see its comment). By this point the switchable windows are
 * already correctly banked by hardware; this only brings the RAM shadows
 * (read by wo_register::get()) into agreement with them, so later code
 * that reads e.g. window2Control.get() sees 1, not a stale post-.bss-zero 0.
 */
__attribute__((constructor(101)))
static void SyncBankShadows() {
    SwitchBank(window1Control, 0);
    SwitchBank(window2Control, 1);
    SwitchBank(window3Control, 2);
}

wo_register<0x9000> chrHighBits;
wo_register<0xe000> chr0Control;
wo_register<0xf000> chr1Control;

/**
 * @brief Establishes VRC1's default CHR bank layout: pattern table 0 =
 *        bank 0, pattern table 1 = bank 1.
 *
 * That's the flat identity mapping matching the project's current single
 * 8 KiB CHR-ROM image (first 4 KiB = pattern table 0, second 4 KiB =
 * pattern table 1) -- i.e. what CHR already looked like before VRC1.
 *
 * Unlike the PRG windows (::_reset), this doesn't need to run before
 * crt0's .bss zeroing or from the fixed bank: CHR-ROM is PPU-addressed,
 * not CPU-addressed, so an unset or momentarily-wrong bank here can't
 * crash anything the way a wrong PRG bank could -- it can only be wrong
 * pixels, and only until this runs, well before the PPU is ever turned on.
 * An ordinary constructor through the normal shadowed path is enough; no
 * need to touch _reset or SyncBankShadows for this.
 */
__attribute__((constructor(101)))
static void InitCHRBankState() {
    SwitchCHRBank0(0);
    SwitchCHRBank1(1);
}
