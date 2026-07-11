#include <platform-nes/interrupts.hpp>
#include <platform-nes/technology.hpp>

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
// DMC cycle-interrupt scheduling — stubbed out. See interrupts.hpp for the
// documented contract of dmc_chain_handler() and ScheduleInterrupt().
// ---------------------------------------------------------------------------
ASM_LINKAGE __attribute__((naked, used))
void dmc_chain_handler() {
    __asm__ ("rti");
}

void ScheduleInterrupt(const irq_pos_t /*location*/, const u16 /*cycles*/,
                       volatile bool* const /*ready*/) {
}
