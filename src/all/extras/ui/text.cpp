#include <platform-nes/extras/ui/text.hpp>

#include "platform-nes/video.hpp"

namespace ui::text {
    void Draw(const u8* buff, const u8 sBuff, const vec2<u16> pos, const vec2<u8> box, const u8 splitter, const Alignment align) {
        u8 cursor    = 0;
        u8 last      = 0;
        u8 lastWhite = 0;
        u8 row       = 0;

        while (cursor < sBuff && row < box.y) {
            for  (; cursor < sBuff && cursor - last < box.x; cursor++) {
                if (*(buff + cursor) == splitter) lastWhite = cursor;
            }

            // ran off the end of the buffer (rather than the width limit) ->
            // print the rest as-is instead of trimming to the last splitter
            const u8 end = (cursor == sBuff) ? cursor : lastWhite;

            const u16 xpos = pos.x + (align == Alignment::Left
                ? 0
                : (box.x - (end - last)) >> 1
            );

            // draw to row
            ppu::WriteFromBufferToNameTable(
                {xpos, static_cast<u16>(pos.y + row)},
                buff + last,
                end - last,
                0
            );

            row++;              // next row
            last   = end + 1;   // bump past the splitter
            cursor = last;
        }
    }
}
