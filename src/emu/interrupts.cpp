#include <platform-nes/interrupts.hpp>
#include <cstdlib>
#include <cstring>

irq_t  irqPending;
bool   irqPendingValid;
u8     scheduledIRQId;

irq_handler_fn* irqTable;
size_t          irqTableCount;
size_t          irqTableCap;

void SetNextIRQHandler(const irq_t handle) {
    irqPending      = handle;
    irqPendingValid = true;
}

irq_t GetCurrentIRQHandler() {
    return irqPendingValid ? irqPending : irq_t{};
}

void RegisterIRQHandler(const u8 id, const irq_handler_fn fn) {
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

void ScheduleInterrupt(const irq_pos_t location, u32 /*cycles*/, volatile bool* /*ready*/) {
    SetNextIRQHandler({ scheduledIRQId, location.x, location.y });
}

extern "C" void dmc_chain_handler() {}

void reset() {
    post();
    exit(0);
}