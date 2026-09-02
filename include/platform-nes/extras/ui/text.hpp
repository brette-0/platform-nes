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
     *
     *  Every row, including the last row of the box, is wrapped to the last
     *  splitter within box.x - the last row is never crammed with as many
     *  characters as physically fit regardless of word boundaries.
     *
     *  Returns progress: the offset into buff of the first byte not yet drawn.
     *  Equal to sBuff when the whole buffer was drawn; less than sBuff when the
     *  box ran out of rows first, so the caller can resume from that offset in
     *  a subsequent box/page.
     */
    u8 Draw(const u8* buff, u8 sBuff, vec2<u16> pos, vec2<u8> box, u8 splitter, Alignment align);

    class ContinuousDrawer {
    public:
        ContinuousDrawer(const u8* buff, u8 sBuff, vec2<u16> pos, vec2<u8> box, u8 splitter, Alignment align);
        bool Next();
        bool Previous();

    private:
        u8 AdvanceToNextLine(u8 offset) const;
        u8 AdvanceToPreviousLine(u8 offset) const;

        u8        sBuff;
        const u8* buff;
        vec2<u16> pos;
        vec2<u8>  box;
        u8        splitter;
        Alignment align;

        u8 lastProgress;
        u8 progress;
    };
}