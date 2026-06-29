#include <platform-nes/interrupts.hpp>
#include <platform-nes/apu.hpp>

irq_t nextHandle;

void SetNextIRQHandler(const irq_t handle) { nextHandle = handle; }
irq_t GetCurrentIRQHandler() { return nextHandle; }

extern "C" __attribute__((used)) void (*irq_fn)() = nullptr;

ASM_LINKAGE __attribute__((naked, used))
void irq() { __asm__ volatile ("jmp (irq_fn)"); }

void reset() {
    /* TODO: banked ROM needs far-call reset */
    __asm__ ("jmp ($fffc)");
}

// ---------------------------------------------------------------------------
// DMC 1-byte sample durations (NTSC CPU cycles), rate index 0..15.
// Each entry = 8 × APU DMC period.  Ordered largest-first for greedy search.
// ---------------------------------------------------------------------------
static constexpr u16 kDmcDuration[16] = {
    3424, 3040, 2720, 2560, 2288, 2032, 1808, 1712,
    1520, 1280, 1136, 1024,  848,  672,  576,  432
};

// One silent byte in the fixed PRG-ROM bank so the DMC DMA has something to
// read.  The DMC start address register ($4012) encodes: addr = $C000 + R*$40,
// so R = (addr - $C000) >> 6.  Placed in the fixed bank; kDmcStart is derived
// at runtime from the resolved symbol address so no linker-script magic is
// required.
[[gnu::section(".prg_rom_fixed.rodata"), gnu::used]]
static const u8 kDmcSilentByte = 0;

// ---------------------------------------------------------------------------
// Chain state -- all in zero page so the naked handler gets 3-cycle access.
//
//   sChainRates[0..sChainCount-1]: DMC rate indices for each note in order.
//   sChainCount:   total notes (intermediate + final).
//   sChainIdx:     index of the NEXT note to arm; starts at 1 (note 0 is
//                  armed directly by ScheduleInterrupt).
//   sChainDmcStart: precomputed $4012 value (sample start address).
//   sChainSndChn:  precomputed $4015 value (existing enables | 0x10).
//   sReadyLo/Hi:   lo/hi bytes of the `ready` pointer; handler writes 1 to
//                  *ready on the last note so HUD_GATE dispatches instead of
//                  denying.
// ---------------------------------------------------------------------------
// C linkage + used: naked asm references these by raw symbol name;
// LTO must not internalize or rename them.  Defined (not declared) inside
// extern "C" {} so the symbols have no C++ mangling.
extern "C" {
__attribute__((used, section(".zp.bss"))) u8 sChainRates[8];
__attribute__((used, section(".zp.bss"))) u8 sChainCount;
__attribute__((used, section(".zp.bss"))) u8 sChainIdx;
__attribute__((used, section(".zp.bss"))) u8 sChainDmcStart;
__attribute__((used, section(".zp.bss"))) u8 sChainSndChn;
__attribute__((used, section(".zp.bss"))) u8 sReadyLo;
__attribute__((used, section(".zp.bss"))) u8 sReadyHi;
}

// ---------------------------------------------------------------------------
// arm_dmc: write the five DMC registers to start a 1-byte silent note.
// $4010 bit 7 = IRQ enable, bits 3:0 = rate index.
// $4013 = 0 -> hardware adds 1 -> 1-byte sample (fires after 8 × period).
// Caller is inside SHADOW(APU_REGISTERS) so these writes are intentionally
// clobbering the APU state; SHADOW restores it after the frame split.
// ---------------------------------------------------------------------------
static inline void arm_dmc(const u8 rate_idx, const u8 dmc_start) {
    apu.dmc_freq  = 0x80u | rate_idx;
    apu.dmc_raw   = 0x00u;
    apu.dmc_start = dmc_start;
    apu.dmc_len   = 0x00u;
    apu.snd_chn   = static_cast<u8>(apu.snd_chn) | 0x10u;
}

// ---------------------------------------------------------------------------
// Naked intermediate chain handler.
//
// Pointed to by irq_fn for all but the final DMC note.  Each invocation:
//   1. Arms the next note from sChainRates[sChainIdx].
//   2. Advances sChainIdx.
//   3. If sChainIdx just reached sChainCount (final note was just armed):
//        sets *ready = 1 and restores irq_fn to HUD_GATE so the final note
//        routes through the FAST_LOCKED_IRQ dispatch path.
//
// Naked (no imaginary-register save/restore) to keep overhead fixed and cheap:
//   7 (hw entry) + ~63 (body, not-last path) = ~70 cycles per intermediate note.
//
// Register contract: only A and X are touched; the caller (hardware ISR entry)
// has not established any register state, so no saves beyond A are needed.
// ---------------------------------------------------------------------------
ASM_LINKAGE __attribute__((naked, used))
void dmc_chain_handler() {
    __asm__ (
        "pha\n\t"                       // 3: save A (X is caller-saved / don't care)

        // Arm the note at sChainRates[sChainIdx]
        "ldx sChainIdx\n\t"             // 3: X = next index (ZP)
        "lda sChainRates,x\n\t"         // 4: A = rate (ZP,X)
        "ora #0x80\n\t"                 // 2: set IRQ-enable bit
        "sta $4010\n\t"                 // 4: rate + IRQ enable
        "lda #0\n\t"                    // 2
        "sta $4011\n\t"                 // 4: DAC direct (no glitch)
        "lda sChainDmcStart\n\t"        // 3: ZP
        "sta $4012\n\t"                 // 4: sample start
        "lda #0\n\t"                    // 2
        "sta $4013\n\t"                 // 4: 1-byte length
        "lda sChainSndChn\n\t"          // 3: ZP (existing enables | 0x10)
        "sta $4015\n\t"                 // 4: enable DMC

        // Advance index and test whether the note just armed is the final one
        "inx\n\t"                       // 2
        "stx sChainIdx\n\t"             // 3: ZP
        "cpx sChainCount\n\t"           // 3: ZP  (X == sChainCount -> final)
        "bcc 1f\n\t"                    // 2/3: branch if more notes remain

        // Final note was just armed: signal ready and restore the real gate
        "ldy #0\n\t"                    // 2
        "lda #1\n\t"                    // 2
        "sta (sReadyLo),y\n\t"          // 6: *ready = 1  (ZP indirect+Y)
        "lda #<HUD_GATE\n\t"            // 2
        "sta irq_fn\n\t"                // 4: restore gate lo
        "lda #>HUD_GATE\n\t"            // 2
        "sta irq_fn+1\n\t"              // 4: restore gate hi

        "1:\n\t"
        "pla\n\t"                       // 4: restore A
        "rti"                           // 6: done
    );
}

// ---------------------------------------------------------------------------
// ScheduleInterrupt
//
// Decomposes `cycles` into the minimum number of DMC notes using a greedy
// largest-first algorithm.  Each intermediate note costs its duration plus
// ~70 cycles of chain-handler overhead; the final note costs its duration
// (the gate's ready-path overhead is a fixed constant the caller accounts for
// in the budget).
//
// For one note  (common case, e.g. 1820 cy -> index 6 = 1808 cy):
//   sets *ready immediately and arms the single note; dmc_chain_handler is
//   never involved.
//
// For N > 1 notes:
//   irq_fn is temporarily redirected to dmc_chain_handler; it is restored to
//   HUD_GATE by the handler itself when it arms the final note.
// ---------------------------------------------------------------------------
void ScheduleInterrupt(const irq_pos_t /*location*/, const u16 cycles,
                       volatile bool* const ready) {
    constexpr u16 kChainOverhead = 70;  // hw(7) + handler not-last path(~63)

    // Compute $4012 from the resolved address of kDmcSilentByte.
    // kDmcSilentByte is in the fixed bank (>=$C000); the cast is exact on 6502.
    const u8 dmc_start = static_cast<u8>(
        (reinterpret_cast<u16>(&kDmcSilentByte) - 0xC000u) >> 6
    );

    // Greedy decomposition: pack intermediate notes while enough budget remains
    // for at least one final note after accounting for chain overhead.
    sChainCount = 0;
    u16 remaining = cycles;

    while (sChainCount < 7) {       // cap: 7 intermediate + 1 final = 8 total
        bool added = false;
        for (u8 i = 0; i < 16; i++) {
            const u16 cost = kDmcDuration[i] + kChainOverhead;
            // Only add this note if the remainder can still fit a final note
            if (cost < remaining && remaining - cost >= kDmcDuration[15]) {
                sChainRates[sChainCount++] = i;
                remaining -= cost;
                added = true;
                break;
            }
        }
        if (!added) break;
    }

    // Final note: largest duration that still fits the remaining budget
    u8 final_rate = 15;         // default to smallest (432 cy)
    for (u8 i = 0; i < 16; i++) {
        if (kDmcDuration[i] <= remaining) { final_rate = i; break; }
    }
    sChainRates[sChainCount] = final_rate;  // append final note at end

    if (sChainCount == 0) {
        // Single note: set ready now and arm; chain handler never runs
        if (ready) *ready = true;
        arm_dmc(final_rate, dmc_start);
    } else {
        // Multi-note: redirect irq_fn to chain handler for intermediate notes.
        // The handler arms notes [1..sChainCount-1] in sequence, then on
        // sChainRates[sChainCount] (the final note) sets *ready and restores
        // irq_fn to HUD_GATE before returning.
        sChainIdx     = 1;
        sChainDmcStart = dmc_start;
        sChainSndChn   = static_cast<u8>(apu.snd_chn) | 0x10u;

        if (ready) {
            const auto addr = reinterpret_cast<u16>(ready);
            sReadyLo = static_cast<u8>(addr);
            sReadyHi = static_cast<u8>(addr >> 8);
        }

        irq_fn = &dmc_chain_handler;
        arm_dmc(sChainRates[0], dmc_start);
    }
}
