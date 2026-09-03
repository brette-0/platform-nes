#include <platform-nes/extras/ui/singlechoice.hpp>

#include "platform-nes/input.hpp"
#include "platform-nes/video.hpp"

namespace ui::choice {
    template <u8 nOptions>
    SingleChoice<nOptions>::SingleChoice(
            u8* buff, const u8 sBuff, const vec2<u16> pos, const vec2<u8> box,
            const u8 wordSplitter, const u8 optionSplitter, const text::Alignment align,
            const u8 emptyGraphic, const u8 arrowGraphic
        ) : option(0), pos(pos), box(box), emptyGraphic(emptyGraphic), arrowGraphic(arrowGraphic) {

        // Arrow sits one tile left of the text with a blank tile of gap in
        // between -- same layout title.cpp's menu arrow uses.
        const u16 arrowCol = pos.x - 2;

        u8 cursor = 0;
        u8 row    = 0;

        for (u8 opt = 0; opt < nOptions && cursor < sBuff; opt++) {
            optionPos[opt] = row;

            // this option's chunk runs up to the next optionSplitter, or the
            // end of the buffer for the last option
            u8 chunkEnd = cursor;
            while (chunkEnd < sBuff && *(buff + chunkEnd) != optionSplitter) chunkEnd++;

            // word-wrap this option's chunk exactly like text::Draw, except
            // the row cursor carries on across options instead of resetting
            u8 last = cursor;
            while (last < chunkEnd && row < box.y) {
                u8   lastWhite  = last;
                bool foundWhite = false;
                u8   c          = last;

                for (; c < chunkEnd && c - last < box.x; c++) {
                    if (*(buff + c) == wordSplitter) {
                        lastWhite  = c;
                        foundWhite = true;
                    }
                }

                const u8 end = (c == chunkEnd) ? c : (foundWhite ? lastWhite : c);

                const u16 xpos = pos.x + (align == text::Alignment::Left
                    ? 0
                    : (box.x - (end - last)) >> 1
                );

                ppu::WriteFromBufferToNameTable(
                    {xpos, static_cast<u16>(pos.y + row)},
                    buff + last,
                    end - last,
                    0
                );

                row++;
                last = (end < chunkEnd) ? end + 1 : end;
            }

            // step past the optionSplitter itself, into the next option's text
            cursor = (chunkEnd < sBuff) ? chunkEnd + 1 : chunkEnd;
        }

        ppu::WriteSingleToNameTable({arrowCol, static_cast<u16>(pos.y + optionPos[0])}, arrowGraphic);
    }

    template <u8 nOptions>
    auto SingleChoice<nOptions>::Pass(const u8 inputs) -> void {
        if      (inputs & input::UP)   Next();
        else if (inputs & input::DOWN) Previous();
    }

    template<u8 nOptions>
    auto SingleChoice<nOptions>::Next() -> void {
        if (option == nOptions - 1) return;

        ppu::WriteSingleToNameTable({static_cast<u16>(pos.x - 2), static_cast<u16>(pos.y + optionPos[option])}, emptyGraphic);
        option++;
        ppu::WriteSingleToNameTable({static_cast<u16>(pos.x - 2), static_cast<u16>(pos.y + optionPos[option])}, arrowGraphic);
    }

    template <u8 nOptions>
    auto SingleChoice<nOptions>::Previous() -> void {
        if (option == 0) return;

        ppu::WriteSingleToNameTable({static_cast<u16>(pos.x - 2), static_cast<u16>(pos.y + optionPos[option])}, emptyGraphic);
        option--;
        ppu::WriteSingleToNameTable({static_cast<u16>(pos.x - 2), static_cast<u16>(pos.y + optionPos[option])}, arrowGraphic);
    }
}
