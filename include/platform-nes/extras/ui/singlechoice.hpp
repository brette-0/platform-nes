#pragma once

#include <intsh>

#include "text.hpp"
#include "platform-nes/types.hpp"
#include "platform-nes/technology.hpp"   // atomic

using namespace br0::intsh;

namespace ui::choice {
    class SingleChoice {
    public:
        SingleChoice(
            const u8* buff, u8 sBuff, vec2<u16> pos, vec2<u8> box,
            u8 wordSplitter, u8 optionSplitter, text::Alignment align,
            u8 emptyGraphic, u8 arrowGraphic, u8 nOptions
        );

        ~SingleChoice();

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
        u8*  optionPos;
        u16* optionAddr;
        const u8 emptyGraphic;
        const u8 arrowGraphic;
        const u8 nOptions;

        auto Step(bool forward, u8*& buf) -> void;
    };
}