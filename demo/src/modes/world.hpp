#pragma once
#include "../banks.hpp"

namespace world {
    // BANKED (bank 0, shared with title -- see banks.hpp): call as
    // mmc3::Call<world::main>(), never directly.
    void main();
}

// See title.hpp's own comment for why the bind lives in the header.
MMC3_BIND(world::main, bank001);
