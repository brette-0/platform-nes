#include <platform-nes/interrupts.h>

irq_t nextHandle;

void SetNextIRQHandler(const irq_t handle) {
    nextHandle = handle;
}