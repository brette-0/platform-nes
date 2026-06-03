#ifndef METATILES_H
#define METATILES_H
#include <cstdint>

extern const std::uint8_t Metatiles[1024];
extern const std::uint8_t MetatileAttributes[64];

#define METATILE_ATTR(id) \
    ((MetatileAttributes[(id) >> 2] >> (((id) & 3) << 1)) & 3)
#endif