#include <platform-nes/interrupts.hpp>
#include <cstdlib>
#include <cstring>

irq_t* irqBuffer;
size_t irqCount;
size_t irqCap;

irq_handler_fn* irqTable;
size_t          irqTableCount;
size_t          irqTableCap;

void SetNextIRQHandler(const irq_t handle) {
    if (irqCount == irqCap) {
        const auto n = irqCap ? irqCap * 2 : 8;
        auto *grown = static_cast<irq_t *>(realloc(irqBuffer, n * sizeof(irq_t)));
        if (!grown) abort();
        irqBuffer = grown;
        irqCap = n;
    }
    irqBuffer[irqCount++] = handle;
}

void RegisterIRQHandler(const std::uint8_t id, const irq_handler_fn fn) {
    if (static_cast<size_t>(id) >= irqTableCap) {
        auto n = irqTableCap ? irqTableCap : 8;
        while (n <= static_cast<size_t>(id)) n *= 2;
        auto *grown = static_cast<irq_handler_fn *>(realloc(irqTable, n * sizeof(irq_handler_fn)));
        if (!grown) abort();
        irqTable = grown;
        memset(irqTable + irqTableCap, 0,
               (n - irqTableCap) * sizeof(irq_handler_fn));
        irqTableCap = n;
    }
    irqTable[id] = fn;
    if (static_cast<size_t>(id) + 1 > irqTableCount) irqTableCount = static_cast<size_t>(id) + 1;
}

void reset() {
    post();
    exit(0);
}