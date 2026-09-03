#pragma once

#include <intsh>

#include "text.hpp"
#include "platform-nes/types.hpp"
#include "platform-nes/technology.hpp"   // atomic

using namespace br0::intsh;

namespace ui::choice {
    class SingleChoice {
    public:
        // Called to clear/draw the selection indicator at addr. buf is
        // opaque to SingleChoice: it's just a cursor into a caller-owned
        // buffer, handed to whichever VisualFn runs and advanced however
        // much (or little) that callback wants -- the encoding of what
        // goes into it, whether anything does at all, and how/when it gets
        // drained back out are entirely the caller's decision. SingleChoice
        // never reads *buf and never writes through it itself; it only
        // decides *when* clear/draw run, never *how* selection is shown.
        using VisualFn = void (*)(u16 addr, u8*& buf);

        // buf is passed straight to draw() for the first option's initial
        // indicator, same as Pass() does for later selection changes --
        // see VisualFn.
        SingleChoice(
            const u8* buff, u8 sBuff, vec2<u16> pos, vec2<u8> box,
            u8 wordSplitter, u8 optionSplitter, text::Alignment align,
            VisualFn clear, VisualFn draw, u8 nOptions, u8*& buf
        );

        ~SingleChoice();

        // Forwards buf, untouched, into whichever of clear/draw ends up
        // running -- see VisualFn.
        auto Pass(u8 inputs, u8*& buf) -> void;
        // atomic: written from wherever Pass() is called (an ISR, if the
        // caller defers input handling to vblank the way title.cpp does)
        // and read from ordinary code -- same cross-context contract as
        // any other ISR-published state in this codebase.
        atomic u8 option;
    private:
        vec2<u16> pos;
        vec2<u8>  box;
        u16* optionAddr;
        const VisualFn clear;
        const VisualFn draw;
        const u8 nOptions;

        auto Step(bool forward, u8*& buf) -> void;
    };
}