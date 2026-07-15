#include <platform-nes/interrupts.hpp>
#include <platform-nes/technology.hpp>

irq_t nextHandle;

void SetNextIRQHandler(const irq_t handle) { nextHandle = handle; }
irq_t GetCurrentIRQHandler() { return nextHandle; }

void reset() {
    /* TODO: banked ROM needs far-call reset */
    __asm__ ("jmp ($fffc)");
}
