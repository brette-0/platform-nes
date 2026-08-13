#pragma once
#include "../banks.hpp"

namespace title {
    // BANKED (bank 0, see banks.hpp): call as mmc3::Call<title::main>(),
    // never as a plain title::main() -- the bank holding it isn't mapped
    // until the farcall switches it in.
    void main();

    // Called (as an ordinary C++ function, not a raw vector jump) from
    // main.cpp's nmiTrampoline/irqTrampoline whenever gameMode == Title.
    // Resident, NOT banked: an interrupt can arrive with any bank mapped.
    void nmi_handler();
    void irq_handler();
}

// Must be visible to every TU that calls mmc3::Call<title::main>(), not just
// to title.cpp -- a bank_of<> specialization has to be declared before the
// first use that instantiates it, which is the call site. That is why this
// lives in the header rather than next to the definition.
MMC3_BIND(title::main, bank001);
