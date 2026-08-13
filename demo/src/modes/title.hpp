#pragma once

namespace title {
    void main();

    // Called (as an ordinary C++ function, not a raw vector jump) from
    // main.cpp's nmiTrampoline/irqTrampoline whenever gameMode == Title.
    void nmi_handler();
    void irq_handler();
}
