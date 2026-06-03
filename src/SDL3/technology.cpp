#include <intsh>
using namespace br0::intsh;
#include <platform-nes/technology.hpp>

void PopulateFromBuffer(u8* target, const u16 offset,
                        const u8* buffer, const u16 sBuffer, const i16 step) {
    const auto base = target + offset;
    for (u16 i = 0; i < sBuffer; i++) base[static_cast<int>(i) * step] = buffer[i];
}

void PopulateFromProvider(u8* target, const u16 offset,
                          u8 (*fn)(u16), const u16 amt, const i16 step) {
    const auto base = target + offset;
    for (u16 i = 0; i < amt; i++) base[static_cast<int>(i) * step] = fn(i);
}
