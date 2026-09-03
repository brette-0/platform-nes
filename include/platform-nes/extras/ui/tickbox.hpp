#pragma once
#include <platform-nes/types.hpp>
#include <intsh>

using namespace br0::intsh;

namespace ui::button {
    class TickBox {
    public:
        TickBox(vec2<u16> pos, bool defaultState);
        // Writes the toggled tile's op at *buf -- 3 bytes (addr-hi, addr-lo,
        // val) -- and advances buf past them, instead of touching the PPU
        // directly. buf must point into a caller-owned buffer with at least
        // 3 bytes left.
        auto Pass(u8 inputs, u8*& buf) -> void;

        bool enabled;
    private:
        auto Toggle(u8*& buf) -> void;
        // Precomputed VRAM address for pos (see ppu::CartesianToAddress) --
        // TickBox's tile never moves, so this is paid once at construction
        // instead of on every Toggle().
        int addr;
    };
}
