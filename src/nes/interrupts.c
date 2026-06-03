#include <platform-nes/interrupts.h>

irq_t nextHandle;

void SetNextIRQHandler(const irq_t handle) {
    nextHandle = handle;
}

void reset() {
    /*
    *   TODO: for banked program ROM, will need to bake reset invoke in fixed bank, then change non-internal invoke
    *         to use far call
    */
    __asm__ ("jmp ($fffc)");
}