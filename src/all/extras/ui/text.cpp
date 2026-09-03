#include <platform-nes/extras/ui/text.hpp>

#include "platform-nes/video.hpp"
#include "platform-nes/technology.hpp"   // CREATE_SEGMENT_KEYWORD / MODULE_PLACEMENT

// WHERE THIS MODULE'S CODE GOES -- see PLATFORM_NES_UI_SECTION's own comment
// in CMakeLists.txt/local.cmake.example. Compulsory, like audio's: no default,
// so the placement decision is never a silent guess. Guarded the same way
// MODULE_PLACEMENT itself is (technology.hpp): this file lives under
// src/all/, compiled for every backend, not just NES-banked ones, so the
// requirement only applies where placement is even possible.
#if defined(TARGET_NES) && defined(NES_MAPPER_BANKSWITCHED)
#ifndef PLATFORM_NES_UI_SECTION
#error "PLATFORM_NES_UI_SECTION is not set. It names the ELF section \
src/all/extras/ui/*.cpp is placed into. Set it from CMake -- \
local.cmake.example has a worked example."
#endif
#endif
#define UI_BANK MODULE_PLACEMENT(PLATFORM_NES_UI_SECTION)

namespace ui::text {
    namespace {
        // Mirrors the per-row wrap/trim decision in Draw() (rightmost
        // splitter within [start, start + boxX), or a raw cut only when no
        // splitter was found or the buffer truly ends here) without any PPU
        // access, so it's safe to call purely to probe line boundaries.
        UI_BANK u8 WrapLineEnd(const u8* buff, u8 sBuff, u8 start, u8 boxX, u8 splitter);
    }

    UI_BANK u8 Draw(const u8* buff, const u8 sBuff, const vec2<u16> pos, const vec2<u8> box, const u8 splitter, const Alignment align) {
        u8 cursor = 0;
        u8 last   = 0;
        u8 row    = 0;

        while (cursor < sBuff && row < box.y) {
            // reset per row: a splitter found on an earlier row must never
            // leak in as this row's wrap point (it could sit before `last`
            // and underflow end - last into a bogus, huge run length)
            u8   lastWhite = last;
            bool foundWhite = false;

            for  (; cursor < sBuff && cursor - last < box.x; cursor++) {
                if (*(buff + cursor) == splitter) {
                    lastWhite  = cursor;
                    foundWhite = true;
                }
            }

            // ran off the end of the buffer (rather than the width limit) ->
            // print the rest as-is instead of trimming to the last splitter.
            // This applies identically on the box's last row: it still wraps
            // at the last splitter unless there is truly no more text.
            const u8 end = (cursor == sBuff) ? cursor : (foundWhite ? lastWhite : cursor);

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

            row++;                                  // next row
            last   = (end < sBuff) ? end + 1 : end;  // bump past the splitter, if any
            cursor = last;
        }

        return last;
    }

    UI_BANK ContinuousDrawer::ContinuousDrawer(
        const u8* buff,      const u8 sBuff,
        const vec2<u16> pos, const vec2<u8> box,
        const u8 splitter,   const Alignment align
        ) : sBuff(sBuff), buff(buff), pos(pos), box(box), splitter(splitter), align(align) {

        lastProgress = 0;
        progress = Draw(buff, sBuff, pos, box, splitter, align);
    }

    UI_BANK u8 ContinuousDrawer::AdvanceToNextLine(const u8 offset) const {
        if (offset >= sBuff) return sBuff;
        return WrapLineEnd(buff, sBuff, offset, box.x, splitter);
    }

    UI_BANK u8 ContinuousDrawer::AdvanceToPreviousLine(const u8 offset) const {
        if (!offset) return 0;

        // Bounded mirror of the forward wrap rule: Draw()/AdvanceToNextLine
        // pick the *rightmost* splitter within [start, start + box.x) as a
        // line's end. Scanning backward, the matching start is the position
        // right after the *leftmost* splitter within (offset - box.x, offset).
        // This only ever looks at up to box.x bytes - no walk back to 0 - so
        // it stays cheap enough to call from an NMI handler.
        const u8 windowStart = (offset > box.x) ? offset - box.x : 0;

        u8   cursor     = offset;
        u8   firstWhite = offset;
        bool foundWhite = false;

        while (cursor > windowStart) {
            cursor--;
            if (*(buff + cursor) == splitter) {
                firstWhite = cursor;
                foundWhite = true;
            }
        }

        return foundWhite ? firstWhite + 1 : windowStart;
    }

    UI_BANK bool ContinuousDrawer::Next() {
        // progress is where the currently drawn box's content ends; once
        // that has reached the end of the buffer there is nothing further
        // down to scroll to.
        if (progress >= sBuff) return false;

        const u8 top = AdvanceToNextLine(lastProgress);

        lastProgress = top;
        progress     = top + Draw(buff + top, sBuff - top, pos, box, splitter, align);
        return true;
    }

    UI_BANK bool ContinuousDrawer::Previous() {
        // lastProgress is the top line's offset; 0 means the box already
        // shows the very start of the buffer, so there's nowhere to scroll up to.
        if (!lastProgress) return false;

        const u8 top = AdvanceToPreviousLine(lastProgress);

        lastProgress = top;
        progress     = top + Draw(buff + top, sBuff - top, pos, box, splitter, align);
        return true;
    }

    namespace {
        UI_BANK u8 WrapLineEnd(const u8* buff, const u8 sBuff, const u8 start, const u8 boxX, const u8 splitter) {
            u8   cursor     = start;
            u8   lastWhite  = start;
            bool foundWhite = false;

            for (; cursor < sBuff && cursor - start < boxX; cursor++) {
                if (*(buff + cursor) == splitter) {
                    lastWhite  = cursor;
                    foundWhite = true;
                }
            }

            const u8 end = (cursor == sBuff) ? cursor : (foundWhite ? lastWhite : cursor);
            return (end < sBuff) ? end + 1 : end;
        }
    }
}
