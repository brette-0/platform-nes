#include "title.hpp"
#include "../main.hpp"

namespace title {
    // Not banked. Called directly from main.cpp's RESET loop.
    void main() {
        gameMode = eGameModes::World;
    }

    // Run from the NMI/IRQ vectors, at a moment nothing controls which bank
    // is mapped -- so if these ever grow a body, they need ::FIXED, the way
    // level's handlers do.
    void nmi_handler() {

    }

    void irq_handler() {

    }
}
