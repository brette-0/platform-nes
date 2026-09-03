#pragma once

#include <intsh>

using namespace br0::intsh;

namespace ui {
    // One pending nametable write: a precomputed VRAM address (see
    // ppu::CartesianToAddress) plus the byte to write there. UI components
    // push these into a caller-owned WriteQueue instead of touching the PPU
    // directly -- the actual pokes happen wherever the caller knows it's
    // safe (e.g. title.cpp's nmi_handler, during vblank), not wherever the
    // interaction happened to be triggered from.
    struct WriteOp {
        int addr;
        u8  val;
    };

    // Thin wrapper around a CALLER-OWNED buffer -- WriteQueue itself holds no
    // storage. buf/cap describe that buffer; count is how many slots are
    // filled. Push() drops the write and returns false once cap is reached
    // rather than writing past the caller's buffer.
    //
    // softStack: a second, separate caller-owned pointer, reserved for a
    // future larger scratch region once a component needs more than `cap`
    // pending writes at once (e.g. a bulk text redraw). Unused by every
    // Push()/component today -- threaded through now so call sites don't
    // need their signatures to change again when that's added.
    struct WriteQueue {
        WriteOp* buf;
        u8 cap;
        u8 count;
        void* softStack;

        bool Push(const WriteOp op) {
            if (count >= cap) return false;
            buf[count++] = op;
            return true;
        }
    };
}
