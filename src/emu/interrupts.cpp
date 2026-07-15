#include <platform-nes/interrupts.hpp>
#include <cstdlib>

irq_t  irqPending;
bool   irqPendingValid;

void SetNextIRQHandler(const irq_t handle) {
    irqPending      = handle;
    irqPendingValid = true;
}

irq_t GetCurrentIRQHandler() {
    return irqPendingValid ? irqPending : irq_t{};
}

void reset() {
    post();
    exit(0);
}