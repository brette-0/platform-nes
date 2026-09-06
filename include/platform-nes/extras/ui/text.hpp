#pragma once

#include <intsh>

#include <platform-nes/technology.hpp>
#include <platform-nes/types.hpp>

using namespace br0::intsh;

// text boxes

namespace ui::text {
    /*
     *  Same wrapping rule as Draw, but instead of writing rows to the
     *  nametable it records where each row would have started and how
     *  long it is.
     *
     *  Returns a heap-allocated array of box.y buffer<u8*> entries --
     *  one per row, caller owns it (delete[] once done). addr points
     *  into buff at that row's first byte, size is the row's length in
     *  bytes, splitter excluded. Rows left unused because the buffer
     *  ran out first are zeroed (addr == nullptr, size == 0).
     */
    NI buffer<u8*>* Make(const u8* buff, u8 sBuff, vec2<u8> box, u8 splitter);

    /*
     *  Draws a Make() result: writes chunks[row] to nametable row
     *  pos.y + row for row in [0, boxY), stopping early the first time
     *  it hits a row Make left zeroed (addr == nullptr) -- i.e. the
     *  buffer ran out before boxY rows were filled.
     *
     *  Does not take ownership of chunks -- caller allocated it via
     *  Make() and is responsible for delete[]ing it, whether or not
     *  this broke out early.
     */
    AI void Draw(const buffer<u8*>* chunks, vec2<u16> pos, u8 boxY);
}
