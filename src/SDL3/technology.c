#include <stdint.h>
#include <platform-nes/technology.h>
#include <platform-nes/video.h>

/* Defined in src/SDL3/video.c — not part of the public video API. */
void OAMPopulateFromBuffer(uint16_t offset, const uint8_t* buffer, uint16_t sBuffer, uint16_t step);
void OAMPopulateFromProvider(uint16_t offset, uint8_t (*fn)(uint16_t), uint16_t amt, uint16_t step);

void PopulateFromBuffer(uint8_t* target, uint16_t offset,
                        const uint8_t* buffer, uint16_t sBuffer, uint16_t step) {
    if (target == (uint8_t*)&oamBuffer) {
        OAMPopulateFromBuffer(offset, buffer, sBuffer, step);
        return;
    }
    uint8_t* base = target + offset;
    for (uint16_t i = 0; i < sBuffer; i++) base[i * step] = buffer[i];
}

void PopulateFromProvider(uint8_t* target, uint16_t offset,
                          uint8_t (*fn)(uint16_t), uint16_t amt, uint16_t step) {
    if (target == (uint8_t*)&oamBuffer) {
        OAMPopulateFromProvider(offset, fn, amt, step);
        return;
    }
    uint8_t* base = target + offset;
    for (uint16_t i = 0; i < amt; i++) base[i * step] = fn(i);
}
