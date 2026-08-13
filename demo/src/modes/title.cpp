#include "title.hpp"
#include "../main.hpp"
#include "../banks.hpp"

namespace title {
    // Banked into .prg_rom_001 (physical bank 0) -- see banks.hpp for what
    // banked code is and isn't allowed to call. Reached only through
    // mmc3::Call<title::main>() (main.cpp's RESET loop), never by a plain
    // call: the bank isn't mapped otherwise.
    TITLE void main() {
        gameMode = eGameModes::World;
    }

    // NOT banked, deliberately: these run from the NMI/IRQ vectors, at a
    // moment nothing controls which bank happens to be mapped, so they must
    // stay in the always-resident image. Only ::main is worth banking here.
    void nmi_handler() {

    }

    void irq_handler() {

    }
}
