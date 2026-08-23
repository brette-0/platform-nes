#include <platform-nes/interrupts.hpp>
#include <platform-nes/technology.hpp>

namespace irq {

volatile bool nmi_done;

void reset() {
    /* TODO: banked ROM needs far-call reset */
    __asm__ ("jmp ($fffc)");
}

} // namespace irq
