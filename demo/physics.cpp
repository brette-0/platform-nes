#include <platform-nes/video.hpp>
#include "physics.hpp"

#include <cstdint>

void CheckInsideTile(const oam_t x, const oam_t y) {
    std::uint8_t tPlayerX = x >> 3;
    std::uint8_t tPlayerY = y >> 3;

    std::uint16_t pActorCollidingTile = CartesianToAddress(x, y);
}
