#include <platform-nes/video.hpp>
#include "physics.hpp"

#include <intsh>
using namespace br0::intsh;

void CheckInsideTile(const oam_t x, const oam_t y) {
    u8 tPlayerX = x >> 3;
    u8 tPlayerY = y >> 3;

    u16 pActorCollidingTile = CartesianToAddress(x, y);
}
