#include <platform-nes/interrupts.hpp>
#include <cstdlib>

irq_t  irqPending;
bool   irqPendingValid;
irq_handler_fn scheduledIRQHandler;

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