#pragma once

#include <intsh>

#include "text.hpp"
#include "platform-nes/types.hpp"
#include "platform-nes/technology.hpp"   // atomic
#include "platform-nes/video.hpp"        // ppu::CartesianToAddress / WriteFromBufferToNameTable

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

        // Word-wraps every option's text (same rule as text::Draw, except
        // the row cursor carries on across options instead of resetting)
        // and computes each option's indicator address into optionAddr --
        // touches nothing PPU-side, safe to run while rendering/NMI is
        // live. Static so it can run before -- or entirely without -- a
        // SingleChoice instance, as long as the caller hands it storage
        // for optionAddr (>= nOptions entries).
        //
        // Returns a heap-allocated array of box.y buffer<u8*> entries --
        // one per row, caller owns it (delete[] once done) and hands it
        // to Draw() below. Same layout as text::Make's result.
        static NI buffer<u8*>* Make(
            const u8* buff, u8 sBuff, vec2<u16> pos, vec2<u8> box,
            u8 wordSplitter, u8 optionSplitter,
            u16* optionAddr, u8 nOptions
        );

        // Convenience wrapper over the static Make() using this instance's
        // own optionAddr/nOptions.
        NI auto Make(
            const u8* buff, u8 sBuff, vec2<u16> pos, vec2<u8> box,
            u8 wordSplitter, u8 optionSplitter
        ) -> buffer<u8*>*;

        // Draws a Make() result -- writes chunks to the nametable (same
        // early-break-on-nullptr rule as text::Draw) and fires the initial
        // indicator draw at optionAddr[0], buf forwarded to it untouched,
        // same contract Pass()/Step() use for later selection changes; see
        // VisualFn. Does not take ownership of chunks -- caller allocated
        // it via Make() and is responsible for delete[]ing it.
        //
        // Pays (x,y)->address (a divide+modulo) exactly once, up front, then
        // walks rows by a plain +32 add -- see the address overload below if
        // even that one division doesn't belong on the caller's hot path.
        // Defined here, not in singlechoice.cpp: ::AI promises the body is
        // copied into every caller, which under GCC + LTO requires the body
        // to be visible at each call site -- see ::AI's own comment in
        // technology.hpp.
        static AI void Draw(
            const buffer<u8*>* const chunks, const vec2<u16> pos, const u8 boxY,
            const VisualFn draw, u16* const optionAddr, u8*& buf
        ) {
            Draw(chunks, ppu::CartesianToAddress(pos), boxY, draw, optionAddr, buf);
        }

        // Address overload of Draw(): @p address is row 0's nametable
        // address (see ::ppu::CartesianToAddress), for a caller that
        // already has it precomputed -- e.g. from inside an ISR, where the
        // divide+modulo the vec2 overload above pays isn't affordable at
        // all. Every later row is address + row*32 (one nametable
        // tile-row), so this does zero division of its own.
        //
        // Only correct within a single nametable page (address's row < 30)
        // -- a caller whose box could cross that boundary needs the vec2
        // overload, which still gets it right via CartesianToAddress.
        static AI void Draw(
            const buffer<u8*>* const chunks, const u16 address, const u8 boxY,
            const VisualFn draw, u16* const optionAddr, u8*& buf
        ) {
            u16 rowAddr = address;
            for (u8 row = 0; row < boxY; row++) {
                if (chunks[row].addr == nullptr) {
                    break;
                }

                ppu::WriteFromBufferToNameTable(rowAddr, chunks[row].addr, chunks[row].size, 0);
                rowAddr = static_cast<u16>(rowAddr + 32);
            }

            // Initial selection indicator -- buf forwarded as-is, same
            // contract Pass()/Step() use for every later selection change;
            // see VisualFn.
            draw(optionAddr[0], buf);
        }

        // Convenience wrapper over the static Draw() using this instance's
        // own optionAddr/draw -- the normal way to draw after construction.
        AI auto Draw(const buffer<u8*>* const chunks, const vec2<u16> pos, const u8 boxY, u8*& buf) -> void {
            Draw(chunks, pos, boxY, draw, optionAddr, buf);
        }

        // Convenience wrapper over the static address-overload Draw() using
        // this instance's own optionAddr/draw.
        AI auto Draw(const buffer<u8*>* const chunks, const u16 address, const u8 boxY, u8*& buf) -> void {
            Draw(chunks, address, boxY, draw, optionAddr, buf);
        }

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