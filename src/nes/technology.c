#include <stdint.h>
#include <platform-nes/technology.h>

void PopulateFromBuffer(uint8_t* target, const uint16_t offset,
                        const uint8_t* buffer, const uint16_t sBuffer, const int16_t step) {
    const auto base = target + offset;
    for (auto i = 0; i < sBuffer; i++) base[i * step] = buffer[i];
}

void PopulateFromProvider(uint8_t* target, const uint16_t offset,
                          uint8_t (*fn)(uint16_t), const uint16_t amt, const int16_t step) {
    const auto base = target + offset;
    for (auto i = 0; i < amt; i++) base[i * step] = fn(i);
}
