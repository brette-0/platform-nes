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

        // Allocates optionAddr and stores the callbacks -- touches nothing
        // PPU-side, so this is safe to run while rendering/NMI is live.
        // Layout and the actual nametable writes happen in Draw(), called
        // separately whenever it's actually safe to poke the PPU.
        SingleChoice(VisualFn clear, VisualFn draw, u8 nOptions);

        ~SingleChoice();

        // Lays out option text into the nametable, computes each option's
        // indicator address into optionAddr, and fires the initial
        // indicator draw -- buf forwarded to it untouched, same contract
        // Pass()/Step() use for later selection changes; see VisualFn.
        // Static so it can run before -- or entirely without -- a
        // SingleChoice instance, as long as the caller hands it storage for
        // optionAddr (>= nOptions entries) and a VisualFn for the indicator.
        static auto Draw(
            const u8* buff, u8 sBuff, vec2<u16> pos, vec2<u8> box,
            u8 wordSplitter, u8 optionSplitter, text::Alignment align,
            VisualFn draw, u16* optionAddr, u8 nOptions, u8*& buf
        ) -> void;

        // Convenience wrapper over the static Draw() using this instance's
        // own optionAddr/draw/nOptions -- the normal way to draw after
        // construction.
        auto Draw(
            const u8* buff, u8 sBuff, vec2<u16> pos, vec2<u8> box,
            u8 wordSplitter, u8 optionSplitter, text::Alignment align,
            u8*& buf
        ) -> void;

        // Forwards buf, untouched, into whichever of clear/draw ends up
        // running -- see VisualFn.
        auto Pass(u8 inputs, u8*& buf) -> void;
        // atomic: written from wherever Pass() is called (an ISR, if the
        // caller defers input handling to vblank the way title.cpp does)
        // and read from ordinary code -- same cross-context contract as
        // any other ISR-published state in this codebase.
        atomic u8 option;
    private:
        u16* optionAddr;
        const VisualFn clear;
        const VisualFn draw;
        const u8 nOptions;

        auto Step(bool forward, u8*& buf) -> void;
    };
}