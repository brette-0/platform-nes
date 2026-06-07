#ifndef METATILES_H
#define METATILES_H
#include <intsh>
using namespace br0::intsh;

extern const u8 Metatiles[1024];
extern const u8 MetatileAttributes[64];
extern const u8 MetatileCollisions[];

#define METATILE_ATTR(id) \
    ((MetatileAttributes[(id) >> 2] >> (((id) & 3) << 1)) & 3)
#endif

enum EMetatileCollisions {
    Clear,
    Solid
};

EMetatileCollisions GetMetatileCollisions(u8 metatile);