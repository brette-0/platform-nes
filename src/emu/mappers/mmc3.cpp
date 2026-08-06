/**
 * @file mmc3.cpp
 * @brief Emu-side (off-NES) MMC3 register storage, CHR bank bookkeeping, and
 *        tile-address translation. Counterpart to src/nes/mappers/mmc3.cpp,
 *        which owns the real $8000/$8001-port-pair hardware writes and is
 *        only built for the NES target -- see mmc3.hpp's own file comment.
 *
 * window1Control/window2Control/chr0Control-chr5Control are declared
 * unconditionally in mmc3.hpp (game code calls ::mmc3::SwitchBank /
 * ::mmc3::SwitchCHRBank on them the same way on every target), so they need
 * a definition on EVERY target, not just NES -- ::Register itself already
 * branches its hardware-poke path on TARGET_NES internally.
 *
 * Real NES hardware performs CHR bank switching in silicon: the PPU's own
 * address bus is what MMC3 intercepts, so nothing in software needs to
 * translate a tile address there. Off-NES, the emu PPU (src/emu/ppu.cpp)
 * instead renders straight from one flat, non-bank-switched CHR-ROM image
 * (::patternTable) -- the rest of this file is the software stand-in for
 * what the cartridge's mapper would otherwise be doing: it tracks which
 * physical CHR bank is currently switched into each of MMC3's six windows,
 * and answers "what's the real offset for this PPU-space tile address" via
 * ::GetTileLMA, bound to the PPU through ::ppu::BindTileTranslator.
 */
#include <platform-nes/mappers/mmc3.hpp>
#include <platform-nes/video.hpp>

mmc3::Register<6> mmc3::window1Control;
mmc3::Register<7> mmc3::window2Control;

mmc3::Register<0> mmc3::chr0Control;
mmc3::Register<1> mmc3::chr1Control;
mmc3::Register<2> mmc3::chr2Control;
mmc3::Register<3> mmc3::chr3Control;
mmc3::Register<4> mmc3::chr4Control;
mmc3::Register<5> mmc3::chr5Control;

bool mmc3::shape = true;   // hardware's mode 0 (large windows at front) -- see mmc3.hpp's own comment.
u8   mmc3::banks[6] = {};

void mmc3::NotifyCHRWrite(const u8 index, const u8 bank) {
    banks[index] = bank;
    ++ppu::chrGeneration;
}

void mmc3::SetCHRMode(const bool largeWindowsAtFront) {
    shape = largeWindowsAtFront;
}

void mmc3::SetMirroring(const bool horizontal) {
    mirroring = horizontal;
}

/**
 * @brief Resolves @p tileVMA through the currently-selected CHR banks.
 *
 * MMC3 CHR windows, mode 0 (::shape true, hardware's own default -- see
 * mmc3.hpp's file comment: PRG mode 0 / CHR mode 0 is the only combination
 * the NES side of this module ever uses):
 *
 *   $0000-$07FF  R0 (2 KiB)   $1000-$13FF  R2 (1 KiB)
 *   $0800-$0FFF  R1 (2 KiB)   $1400-$17FF  R3 (1 KiB)
 *                             $1800-$1BFF  R4 (1 KiB)
 *                             $1C00-$1FFF  R5 (1 KiB)
 *
 * Mode 1 (::shape false) swaps the two halves: R2-R5 at $0000-$0FFF (1 KiB
 * each), R0/R1 at $1000-$1FFF (2 KiB each). Bank numbers are already in
 * their register's own granularity (chr0Control/chr1Control: 2 KiB units;
 * chr2Control-chr5Control: 1 KiB units -- see mmc3.hpp's own comment on
 * ::SwitchCHRBank), so the physical offset is simply bank * window size,
 * plus @p tileVMA's offset within that window.
 */
u32 mmc3::GetTileLMA(const u16 tileVMA) {
    constexpr u32 k2K = 0x800;
    constexpr u32 k1K = 0x400;
    const u16 vma = tileVMA & 0x1FFF;

    if (shape) {
        if (vma < 0x0800) return banks[0] * k2K + (vma - 0x0000);
        if (vma < 0x1000) return banks[1] * k2K + (vma - 0x0800);
        if (vma < 0x1400) return banks[2] * k1K + (vma - 0x1000);
        if (vma < 0x1800) return banks[3] * k1K + (vma - 0x1400);
        if (vma < 0x1C00) return banks[4] * k1K + (vma - 0x1800);
        return banks[5] * k1K + (vma - 0x1C00);
    }

    if (vma < 0x0400) return banks[2] * k1K + (vma - 0x0000);
    if (vma < 0x0800) return banks[3] * k1K + (vma - 0x0400);
    if (vma < 0x0C00) return banks[4] * k1K + (vma - 0x0800);
    if (vma < 0x1000) return banks[5] * k1K + (vma - 0x0C00);
    if (vma < 0x1800) return banks[0] * k2K + (vma - 0x1000);
    return banks[1] * k2K + (vma - 0x1800);
}
