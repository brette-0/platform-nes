#include <platform-nes/interrupts.h>

extern irq_t nextHandle;

void SetNextIRQHandler(const irq_t handle) {
    nextHandle = handle;
}