#include "world.hpp"
#include "../main.hpp"
#include "../banks.hpp"

namespace world {
    // Banked into .prg_rom_001, the SAME physical bank ::TITLE uses -- two
    // keywords, one 8 KiB bank (see banks.hpp). Both areas' code shares that
    // space; if either outgrows it, the linker says so at link time
    // ("section .prg_rom_001 ... does not fit in region") rather than
    // silently overflowing into a bank nothing maps.
    WORLD void main() {
        gameMode = eGameModes::Level;
    }
}
