#include <platform-nes/video.hpp>
#include "physics.hpp"

#include <intsh>
using namespace br0::intsh;

u8 CheckInsideTile(const oam::oam_t x, const oam::oam_t y) {
    const u8 tPlayerX = x >> 3;
    const u8 tPlayerY = y >> 3;

    const u16 collidingTile = ppu::CartesianToAddress(tPlayerX, tPlayerY);

    atomic u8 tile = 0;
    ppu::StreamFromVideoMemory(collidingTile, &tile, 1);
    return tile;
}
