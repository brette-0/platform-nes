#pragma once

#include <intsh>

#include "text.hpp"
#include "platform-nes/types.hpp"
#include "platform-nes/technology.hpp"   // atomic

using namespace br0::intsh;

namespace ui::choice {
    template <u8 nOptions>
    class SingleChoice {
    public:
        SingleChoice(
            const u8* buff, u8 sBuff, vec2<u16> pos, vec2<u8> box,
            u8 wordSplitter, u8 optionSplitter, text::Alignment align,
            u8 emptyGraphic, u8 arrowGraphic
        );

        // Writes up to 2 pending ops (erase-old + draw-new) at *buf -- 3
        // bytes each (addr-hi, addr-lo, val) -- and advances buf past them,
        // instead of touching the PPU directly. buf must point into a
        // caller-owned buffer with enough room left (2 ops = 6 bytes).
        // Caller (e.g. title.cpp's nmi_handler) replays the bytes wherever
        // writing is actually safe.
        auto Pass(u8 inputs, u8*& buf) -> void;
        // atomic: written from wherever Pass() is called (an ISR, if the
        // caller defers input handling to vblank the way title.cpp does)
        // and read from ordinary code -- same cross-context contract as
        // any other ISR-published state in this codebase.
        atomic u8 option;
    private:
        vec2<u16> pos;
        vec2<u8>  box;
        u8 optionPos[nOptions]{};
        // Precomputed arrow-column VRAM address for each option's row, via
        // ppu::CartesianToAddress -- Next()/Previous() run inside the vblank
        // window (see title.cpp's nmi_handler), where the (x,y)->address
        // divide+modulo ppu::WriteSingleToNameTable(vec2,u8) does internally
        // is expensive enough to blow the frame budget. Paying that cost
        // once here, off the hot path, lets Next()/Previous() use the
        // address overload instead -- three register pokes, no arithmetic.
        int optionAddr[nOptions]{};
        const u8 emptyGraphic;
        const u8 arrowGraphic;

        // Next()/Previous() used to be separate member functions -- mirror
        // images of each other except for the increment direction and the
        // boundary check. Merged into one so their WriteOpTo(emptyGraphic)/
        // WriteOpTo(arrowGraphic) pair (each WriteOpTo call inlines, see its
        // own comment in singlechoice.cpp) exists in the emitted code once
        // per instantiation instead of twice.
        auto Step(bool forward, u8*& buf) -> void;
    };
}