#pragma once

#include <intsh>

#include "platform-nes/types.hpp"

using namespace br0::intsh;

// text boxes

namespace ui::text {
    enum class Alignment {
        Left,
        Centre
    };

    /*
     *  This function presumes no word will go unsplit by the splitter for longer
     *  than (box.x - 1)
     */
    void Draw(const u8* buff, u8 sBuff, vec2<u16> pos, vec2<u8> box, u8 splitter, Alignment align);
}