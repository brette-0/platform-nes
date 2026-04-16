#include <stdint.h>
#include <platform-nes/technology.h>

void PopulateFromBuffer(uint8_t* target, const uint16_t offset,
                        const uint8_t* buffer, const uint16_t sBuffer, const uint16_t step) {
    uint8_t* base = target + offset;
    for (uint16_t i = 0; i < sBuffer; i++) base[i * step] = buffer[i];
}

void PopulateFromProvider(uint8_t* target, const uint16_t offset,
                          uint8_t (*fn)(uint16_t), const uint16_t amt, const uint16_t step) {
    uint8_t* base = target + offset;
    for (uint16_t i = 0; i < amt; i++) base[i * step] = fn(i);
}
