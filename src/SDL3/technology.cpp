#include <cstdint>
#include <platform-nes/technology.hpp>

void PopulateFromBuffer(std::uint8_t* target, const std::uint16_t offset,
                        const std::uint8_t* buffer, const std::uint16_t sBuffer, const std::int16_t step) {
    const auto base = target + offset;
    for (std::uint16_t i = 0; i < sBuffer; i++) base[static_cast<int>(i) * step] = buffer[i];
}

void PopulateFromProvider(std::uint8_t* target, const std::uint16_t offset,
                          std::uint8_t (*fn)(std::uint16_t), const std::uint16_t amt, const int16_t step) {
    const auto base = target + offset;
    for (std::uint16_t i = 0; i < amt; i++) base[static_cast<int>(i) * step] = fn(i);
}
