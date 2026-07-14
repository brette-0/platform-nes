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

void ScheduleInterrupt(const irq_pos_t location, u8 /*steps*/, volatile bool* /*ready*/) {
    SetNextIRQHandler({ scheduledIRQHandler, location.x, location.y });
}

extern "C" void dmc_chain_handler() {}

void reset() {
    post();
    exit(0);
}