#include <stdint.h>
#include <platform-nes/technology.h>

void PopulateFromBuffer(uint8_t* target, uint16_t offset,
                        const uint8_t* buffer, uint16_t sBuffer, int16_t step) {
    const auto base = target + offset;
    for (uint16_t i = 0; i < sBuffer; i++) base[(int)i * step] = buffer[i];
}

void PopulateFromProvider(uint8_t* target, uint16_t offset,
                          uint8_t (*fn)(uint16_t), uint16_t amt, int16_t step) {
    const auto base = target + offset;
    for (uint16_t i = 0; i < amt; i++) base[(int)i * step] = fn(i);
}
