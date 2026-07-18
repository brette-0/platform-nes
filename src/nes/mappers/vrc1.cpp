#include <platform-nes/mappers/vrc1.hpp>

wo_register<0x8000> window1Control;
wo_register<0xa000> window2Control;
wo_register<0xc000> window3Control;

extern "C" void _start();

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