#include <platform-nes/extras/ui/singlechoice.hpp>

#include "platform-nes/input.hpp"
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
#define UI_BANK MODULE_PLACEMENT(PLATFORM_NES_UI_SECTION) MINSIZE

namespace ui::choice {
    namespace {
        // Encodes one pending write as 3 bytes (addr-hi, addr-lo, val) at
        // *buf, then advances buf past them. Caller guarantees room is left
        // in the buffer buf points into. Not UI_BANK: tiny and internal-
        // linkage, cheaper inlined into Next()/Previous() than pulled out
        // as its own banked call.
        inline AI void WriteOpTo(u8*& buf, const int addr, const u8 val) {
            *buf++ = static_cast<u8>(addr >> 8);
            *buf++ = static_cast<u8>(addr & 0xFF);
            *buf++ = val;
        }

        // ppu::CartesianToAddress carries no placement/noinline of its own
        // (PLATFORM_NES_VIDEO_SECTION is unset in this project), so LLVM is
        // free to inline it -- and the constructor's per-option loop below
        // has a compile-time trip count (nOptions), which made LLVM unroll
        // the loop AND duplicate the fully-inlined divide/modulo body once
        // per option. NI forces a single real out-of-line copy that the
        // (possibly still-unrolled) loop just calls into instead. Tried
        // `#pragma nounroll` on the loop as a less invasive alternative --
        // it does stop the duplication, but leaves one full inlined copy
        // in place of the jsr, which measured 194 bytes worse than this.
        NI u16 ComputeOptionAddr(const u16 arrowCol, const u16 y) {
            return ppu::CartesianToAddress({arrowCol, y});
        }
    }

    template <u8 nOptions>
    UI_BANK SingleChoice<nOptions>::SingleChoice(
            const u8* buff, const u8 sBuff, const vec2<u16> pos, const vec2<u8> box,
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

        // Pay the (x,y)->address divide+modulo (ppu::CartesianToAddress,
        // internally the same cost ppu::WriteSingleToNameTable(vec2,u8)
        // would pay per call) once per option, here at construction time --
        // not on every Next()/Previous() call, which runs inside the vblank
        // window.
        for (u8 opt = 0; opt < nOptions; opt++) {
            optionAddr[opt] = ComputeOptionAddr(arrowCol, static_cast<u16>(pos.y + optionPos[opt]));
        }

        ppu::WriteSingleToNameTable(optionAddr[0], arrowGraphic);
    }

    template <u8 nOptions>
    UI_BANK auto SingleChoice<nOptions>::Pass(const u8 inputs, u8*& buf) -> void {
        // UP moves the cursor up the list (decrements option), DOWN moves it
        // down (increments) -- matches ui::Canvas's clamp convention and the
        // hand-rolled title-menu logic this replaced.
        if      (inputs & input::UP)   Step(false, buf);
        else if (inputs & input::DOWN) Step(true, buf);
    }

    template<u8 nOptions>
    UI_BANK auto SingleChoice<nOptions>::Step(const bool forward, u8*& buf) -> void {
        if (forward ? option == nOptions - 1 : option == 0) return;

        // optionAddr[]: precomputed at construction, see its own comment --
        // no (x,y)->address divide+modulo here, just the two writes encoded
        // for whoever replays the bytes to poke.
        WriteOpTo(buf, optionAddr[option], emptyGraphic);
        // ++/-- on a volatile member is deprecated (C++20)
        option = static_cast<u8>(forward ? option + 1 : option - 1);
        WriteOpTo(buf, optionAddr[option], arrowGraphic);
    }

    // Explicit instantiation: SingleChoice's members are defined here (not
    // inline in the header, unlike a constexpr template such as
    // demo/src/types.hpp's Regional<>) -- MODULE_PLACEMENT/UI_BANK needs a
    // real, single out-of-line copy of each member to place into a section,
    // which a header-inline template can't provide (every including TU
    // would get its own copy). That means, unlike Regional<>'s own
    // instantiations (demo/src/types.cpp), a consumer can't instantiate
    // SingleChoice<N> itself from a TU that only sees the header -- the
    // member definitions have to be visible where the instantiation happens,
    // so it has to live here.
    //
    // 3 and 4 are the option counts demo/src/modes/title.cpp's menu
    // currently needs (3 on console, 4 on PC targets with the extra Quit
    // option -- see title.cpp's kMenuOptions). Add another instantiation
    // here if a future SingleChoice<N> with a different N is needed.
    template class SingleChoice<3>;
    template class SingleChoice<4>;
}
