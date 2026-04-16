#include <platform-nes/interrupts.h>
#include <stdlib.h>
#include <string.h>

irq_t* irqBuffer;
size_t irqCount;
size_t irqCap;

irq_handler_fn* irqTable;
size_t          irqTableCount;
size_t          irqTableCap;

void SetNextIRQHandler(const irq_t handle) {
    if (irqCount == irqCap) {
        size_t n = irqCap ? irqCap * 2 : 8;
        irqBuffer = realloc(irqBuffer, n * sizeof(irq_t));
        irqCap = n;
    }
    irqBuffer[irqCount++] = handle;
}

void RegisterIRQHandler(uint8_t id, irq_handler_fn fn) {
    if ((size_t)id >= irqTableCap) {
        size_t n = irqTableCap ? irqTableCap : 8;
        while (n <= (size_t)id) n *= 2;
        irqTable = realloc(irqTable, n * sizeof(irq_handler_fn));
        memset(irqTable + irqTableCap, 0,
               (n - irqTableCap) * sizeof(irq_handler_fn));
        irqTableCap = n;
    }
    irqTable[id] = fn;
    if ((size_t)id + 1 > irqTableCount) irqTableCount = (size_t)id + 1;
}
