#include "world.hpp"
#include "../main.hpp"

namespace world {
    // Not banked: 6 bytes, and level owns both switchable windows so there is
    // no spare bank to put it in. Called directly from main.cpp's RESET loop.
    void main() {
        gameMode = eGameModes::Level;
    }
}
