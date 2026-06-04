#ifndef PHYSICS_H
#define PHYSICS_H

#include <platform-nes/video.hpp>

/**
 * @brief Reads the nametable tile the actor at (@p x, @p y) overlaps.
 * @param x Actor pixel X.
 * @param y Actor pixel Y.
 * @return  The nametable byte at the actor's tile position.
 */
u8 CheckInsideTile(oam::oam_t x, oam::oam_t y);

#endif
