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
// DMC chained-note scheduling. See interrupts.hpp for the documented
// contract of ScheduleInterrupt().
//
// A single-byte DMC note's IRQ fires almost immediately after arming:
// bytes-remaining hits zero at DMA FETCH time (the lone byte is fetched
// right away, since the sample buffer starts empty), not after that byte's
// rate-gated playback finishes -- confirmed empirically, every rate fired
// at the same ~280-320 cycles. The rate register is therefore irrelevant at
// this note length. The length register can't express "2 bytes" either
// (16*L+1 jumps straight from 1 to 17 bytes), and 17 bytes' minimum delay
// (16*8*period, 7344+ cycles even at the fastest rate) overshoots this
// call site's few-thousand-cycle budget by a wide margin.
//
// The workaround: chain several 1-byte notes back to back. `steps` picks
// how many links to chain; each link is one DMA-fetch-dominated fire
// (~280-320 cycles) plus this handler's own re-arm overhead, so total delay
// is roughly (steps+1) times that fixed per-link cost -- not rate-selected,
// since rate can't move it at len=0. Calibrate `steps` empirically against
// the target (see the demo's HUD-split IRQ and its spin-wait iteration
// counter) -- the per-link cost isn't an exact hardware constant, just a
// small, repeatable one.
// ---------------------------------------------------------------------------
namespace {
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

    atomic u8 dmcChainRemaining;

    // Rate is irrelevant at len=0 (see file comment) -- always use the
    // fastest, purely so any background DAC output from the note-in-flight
    // dies out as quickly as possible.
    void ArmOneByteNote() {
        apu::dmc_start = static_cast<u8>((reinterpret_cast<u16>(&kDmcSilentSample) - 0xC000) >> 6);
        apu::dmc_len  = 0;                                              // single byte, always
        apu::dmc_freq = 0x80 | 15;                                      // IRQ enable, loop off, fastest rate
        apu::snd_chn  = static_cast<u8>(apu::snd_chn.get() | 0x10);     // re-enable DMC only; other channels untouched
    }
} // namespace

// Cross-TU chain lock for ::FAST_LOCKED_IRQ_CHAINED (see interrupts.hpp) --
// defined here with external linkage so a caller's gate macro expansion can
// reference it by raw symbol name.
volatile bool dmcChainLock;

// Deny-path target for ::FAST_LOCKED_IRQ_CHAINED. While dmcChainRemaining is
// nonzero, re-arms a fresh 1-byte note and counts down; once it hits zero,
// sets dmcChainLock so the gate dispatches to the real handler on the next
// fire. See the file comment above for why this exists and what it's
// working around.
interrupt dmc_chain_handler() {
    if (dmcChainRemaining) {
        dmcChainRemaining = dmcChainRemaining - 1;   // avoids -Wdeprecated-volatile on atomic --
        ArmOneByteNote();
        if (dmcChainRemaining == 0) dmcChainLock = true;
    }
}

void ScheduleInterrupt(const irq_pos_t /*location*/, const u8 steps,
                       volatile bool* const ready) {
    dmcChainLock      = (steps == 0);
    dmcChainRemaining = steps;
    ArmOneByteNote();

    if (ready) *ready = true;
}
