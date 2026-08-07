#pragma once
#include <platform-nes/headers/nes2.hpp>

constexpr u32 sProgramROM   = 32768;
constexpr u32 sCharacterROM = 8192;
constexpr u32 sProgramRAM   = 8192;

NES2_PLACE_HEADER(
    nes2::Header()
        .withMirroring(Mirroring::Horizontal)
        .withBattery(false)
        .withPrgRom(sProgramROM)
        .withChrRom(sCharacterROM)
        .withPrgRam(sProgramRAM)
        .withTiming(Timing::NTSC)
);
