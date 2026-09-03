#pragma once
#include <platform-nes/types.hpp>
#include <platform-nes/extras/ui/writeop.hpp>
#include <intsh>

using namespace br0::intsh;

namespace ui::button {
    class TickBox {
    public:
        TickBox(vec2<u16> pos, bool defaultState);
        // Queues the toggled tile's write into q instead of touching the
        // PPU directly -- see WriteQueue's own comment.
        auto Pass(u8 inputs, WriteQueue& q) -> void;

        bool enabled;
    private:
        auto Toggle(WriteQueue& q) -> void;
        // Precomputed VRAM address for pos (see ppu::CartesianToAddress) --
        // TickBox's tile never moves, so this is paid once at construction
        // instead of on every Toggle().
        int addr;
    };
}
