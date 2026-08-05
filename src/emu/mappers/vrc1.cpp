/**
 * @file vrc1.cpp
 * @brief Emu-side (off-NES) VRC1 register storage and tile-address
 *        translation. Counterpart to src/nes/mappers/vrc1.cpp, which owns
 *        the real hardware port writes and is only built for the NES
 *        target -- see vrc1.hpp's own file comment.
 *
 * VRC1::window1Control/window2Control/window3Control/chrHighBits/chr0Control/
 * chr1Control are declared unconditionally in vrc1.hpp, so they need a
 * definition on every target, not just NES, the same reasoning as MMC3's
 * emu-side mmc3.cpp: ::VRC1::SwitchBank/::VRC1::SwitchCHRBank already branch
 * their hardware-poke path on TARGET_NES internally.
 *
 * Real NES hardware performs CHR bank switching in silicon; off-NES, the emu
 * PPU (src/emu/ppu.cpp) instead renders straight from one flat, non-bank-
 * switched CHR-ROM image (::patternTable), so this file is the software
 * stand-in for what VRC1 would otherwise be doing on the PPU's own address
 * bus: it tracks which physical 4 KiB CHR bank is currently switched into
 * each of VRC1's two pattern-table windows, and answers "what's the real
 * offset for this PPU-space tile address" via ::VRC1::GetTileLMA, bound to
 * the PPU through ::ppu::BindTileTranslator.
 */
#include <platform-nes/mappers/vrc1.hpp>

tech::wo_register<0x8000> VRC1::window1Control;
tech::wo_register<0xa000> VRC1::window2Control;
tech::wo_register<0xc000> VRC1::window3Control;

tech::wo_register<0x9000> VRC1::chrHighBits;
tech::wo_register<0xe000> VRC1::chr0Control;
tech::wo_register<0xf000> VRC1::chr1Control;

u8 VRC1::chrBanks[2] = {};

/**
 * @brief Resolves @p tileVMA through ::chrBanks.
 *
 * VRC1's two CHR windows are a straight 4 KiB/4 KiB split with no mode bit
 * to account for (unlike MMC3): PPU $0000-$0FFF always sources chr0Control's
 * bank, $1000-$1FFF always sources chr1Control's -- so this is a plain
 * bank * window size + in-window offset, no branching on shape needed.
 */
u32 VRC1::GetTileLMA(const u16 tileVMA) {
    constexpr u32 kWindow = 0x1000;
    const u16 vma = tileVMA & 0x1FFF;
    if (vma < 0x1000) return static_cast<u32>(chrBanks[0]) * kWindow + vma;
    return static_cast<u32>(chrBanks[1]) * kWindow + (vma - 0x1000);
}
