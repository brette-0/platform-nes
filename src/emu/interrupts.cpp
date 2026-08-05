#include <platform-nes/interrupts.hpp>
#include <cstdlib>

namespace irq {

irq_t  irqPending;
bool   irqPendingValid;

void reset() {
    post();
    exit(0);
}

} // namespace irq
