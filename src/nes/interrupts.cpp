#include <platform-nes/interrupts.hpp>
#include <platform-nes/technology.hpp>
#include <platform-nes/apu.hpp>

irq_t nextHandle;

void SetNextIRQHandler(const irq_t handle) { nextHandle = handle; }
irq_t GetCurrentIRQHandler() { return nextHandle; }

extern "C" {
__attribute__((used, section(".bss"))) void (*irqTrampoline)();
}

ASM_LINKAGE __attribute__((naked, used))
void irq() { __asm__ volatile ("jmp (irqTrampoline)"); }

void reset() {
    /* TODO: banked ROM needs far-call reset */
    __asm__ ("jmp ($fffc)");
}

// ---------------------------------------------------------------------------
// DMC cycle-interrupt scheduling. See interrupts.hpp for the documented
// contract of ScheduleInterrupt().
//
// One note, one completion IRQ -- no chaining, no gate. $4013 (sample
// length) is a full 8-bit register: bytes = L*16 + 1, up to 4081 bytes, and
// at the slowest rate (428 cycles/bit, 3424 cycles/byte) that reaches
// ~13.9M cycles, roughly 466 NTSC frames. That's far beyond anything a
// single scheduled IRQ needs, so there's no reason to split the wait across
// multiple notes: SetIRQ points straight at the target handler (see the
// demo's HUD IRQ), and every DMC IRQ that fires is the real one.
// ---------------------------------------------------------------------------
namespace {
    // NTSC DMC rate-table periods (NESDev), scaled to cycles-per-byte: a
    // byte is 8 bits, i.e. 8x the per-bit period. Index == the 4-bit rate
    // written to $4010.
    constexpr u32 kDmcByteCycles[16] = {
        3424, 3040, 2720, 2560, 2288, 2032, 1808, 1712,
        1520, 1280, 1136, 1024,  848,  672,  576,  432
    };

    // The DMC always plays from ROM, so arming needs one real byte to point
    // its sample-start register at. On bankswitched builds this must stay
    // reachable regardless of which bank is currently paged in -- same
    // reasoning as RESET's own .prg_rom_fixed pin in interrupts.hpp. 0xAA
    // alternates the delta-modulation bit every step, so the DAC output
    // stays roughly centred instead of ramping to a rail and clicking.
#ifdef NES_MAPPER_BANKSWITCHED
    __attribute__((section(".prg_rom_fixed"), aligned(64)))
#endif
    constexpr u8 kDmcSilentSample = 0xAA;
} // namespace

// ScheduleInterrupt below always resolves in a single note (see the file
// comment), so nothing ever denies into this. Kept as a valid, inert deny
// target so ::FAST_LOCKED_IRQ_CHAINED remains usable on its own terms if
// something else ever wants real note-to-note chaining.
ASM_LINKAGE __attribute__((naked, used))
void dmc_chain_handler() {
    __asm__ ("rti");
}

void ScheduleInterrupt(const irq_pos_t /*location*/, const u32 cycles,
                       volatile bool* const ready) {
    apu::dmc_start = static_cast<u8>((reinterpret_cast<u16>(&kDmcSilentSample) - 0xC000) >> 6);

    // Largest per-byte rate that still fits within the budget in a single
    // byte; for anything longer than that, stretch the sample length
    // instead (rounded to the nearest 16-byte step -- coarse, but this is
    // rudimentary by design and "close enough" for a multi-frame wait).
    u8 rate = 15;
    for (u8 i = 0; i < 15; ++i) {
        if (cycles >= kDmcByteCycles[i]) { rate = i; break; }
    }

    const u32 perByte = kDmcByteCycles[rate];
    u32 bytesWanted = cycles / perByte;
    if (bytesWanted < 1) bytesWanted = 1;
    const u32 l = (bytesWanted - 1 + 8) / 16;    // round to nearest step
    apu::dmc_len = static_cast<u8>(l > 255 ? 255 : l);

    apu::dmc_freq = 0x80 | rate;                                   // IRQ enable, loop off
    apu::snd_chn  = static_cast<u8>(apu::snd_chn.get() | 0x10);    // re-enable DMC only; other channels untouched

    if (ready) *ready = true;
}
