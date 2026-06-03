#include <platform-nes/video.h>
#include "physics.h"

void CheckInsideTile(const oam_t x, const oam_t y) {
    uint8_t tPlayerX = x >> 3;
    uint8_t tPlayerY = y >> 3;

    uint16_t pActorCollidingTile = CartesianToAddress(x, y);


}
