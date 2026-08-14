#pragma once

namespace title {
    // Not banked: 10 bytes of code, and no spare bank to put it in -- level
    // owns both switchable windows. Called directly from main.cpp.
    void main();

    // Called (as an ordinary C++ function, not a raw vector jump) from
    // main.cpp's nmiTrampoline/irqTrampoline whenever gameMode == Title.
    void nmi_handler();
    void irq_handler();
}
