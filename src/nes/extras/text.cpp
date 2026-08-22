#include <platform-nes/extras/text.hpp>

#include "platform-nes/video.hpp"

namespace ui::text {
    void DrawText(const u8* buff, const u8 sBuff, const vec2<u16> pos, const vec2<u8> box, const u8 splitter, const Alignment align) {
        u8 cursor    = 0;
        u8 last      = 0;
        u8 lastWhite = 0;
        u8 row       = 0;

        while (cursor < sBuff && row < box.y) {
            for  (; cursor < sBuff && cursor - last < box.x; cursor++) {
                if (*(buff + cursor) == splitter) lastWhite = cursor;
            }

            last   = cursor;
            cursor = lastWhite;

            const u16 xpos = pos.x + (align == Alignment::Left
                ? 0
                : box.x >> 1
            );

            // draw to row
            ppu::WriteFromBufferToNameTable(
                xpos, pos.y + row,
                buff + last,
                cursor - last,
                0
            );

            row++;              // next row
            cursor++; last++;   // bump past the leading whitespace
        }
    }
}
