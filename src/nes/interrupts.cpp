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
