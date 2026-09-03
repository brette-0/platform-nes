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
 * ::GetTileLMA. This file also supplies the strong (non-weak) definition of
 * ::ppu::ResolveTile the emu PPU calls unconditionally for every tile fetch
 * -- see that function's own doc comment (video.hpp) for why this always
 * wins over the emu PPU's own weak default with no runtime dispatch, and
 * ALTERNATIVE_NAMETABLE == 1 (four-screen)'s own comments below for the
 * matching ::ppu::ReadNametable/::WriteNametable/::nametableRows/
 * ::InitCartVRAM/::ppu::Flush overrides.
 */
#include <platform-nes/mappers/mmc3.hpp>
#include <platform-nes/video.hpp>
#include <platform-nes/interrupts.hpp>

#include <cstdlib>

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
 * @brief Off-NES stand-in for the real $C000/$C001/$E001 MMC3 IRQ-arming
 * sequence: there's no scanline-counter hardware to poke, so this just arms
 * the shared single-slot ::irq::irqPending mechanism with the application's
 * fixed ::IRQ entry point (::irq_vector) at @p position -- the same handler
 * the real hardware IRQ vector would reach, only the position varies.
 * Matches ::video::WaitThenReactToSpriteZero's shape, but always targets
 * ::irq_vector rather than a caller-supplied callback, since a real
 * interrupt source has exactly one destination, chosen at compile time --
 * never a runtime-supplied function pointer.
 *
 * @p scanline plays no part in this -- see this function's own doc comment
 * (mmc3.hpp) for why it's specifically NOT reused to derive @p position.
 */
void mmc3::ScheduleScanlineIRQ(const u8, const vec2<u16> position) {
    irq::irqHandler      = irq_vector;
    irq::irqPosition     = position;
    irq::irqPendingValid = true;
}

/** @brief Off-NES stand-in for the real $E000 disable+acknowledge write. */
void mmc3::AcknowledgeScanlineIRQ() {
    irq::irqPendingValid = false;
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
 * each), R0/R1 at $1000-$1FFF (2 KiB each).
 *
 * Bank numbers are ALL in 1 KiB units, including R0/R1: real MMC3 silicon
 * counts every CHR bank register in 1 KiB units regardless of how wide the
 * window it controls is, and R0/R1 (2 KiB windows) simply ignore the bottom
 * bit of the value written to them -- the register can only ever latch an
 * even 1 KiB-granularity bank, which is what makes a 2 KiB-aligned window
 * out of it (see mmc3.hpp's own comment on ::SwitchCHRBank; confirmed
 * against nesdev.org/wiki/MMC3: "R0 and R1 ignore the bottom bit, as the
 * value written still counts banks in 1KB units but odd numbered banks
 * can't be selected"). So the physical offset is `(bank & ~1) * 1 KiB`,
 * for every register including R0/R1 -- NOT `bank * 2 KiB` -- plus @p
 * tileVMA's offset within that window.
 *
 * The result is wrapped modulo ::ppu::chrRomBytes: a bank register can be
 * (and, in practice, is) written with a value that addresses past what this
 * build actually embeds -- e.g. a board whose real CHR-ROM chip is bigger
 * than the CHR art currently linked into a desktop preview build. Real
 * hardware doesn't fault on that; the chip's own address pins are wired to
 * only as many bits as its actual capacity needs, so a too-large address
 * simply aliases back into the chip's real range. This mirrors that in
 * software, rather than indexing ::patternTable (::CHR_ROM) out of bounds.
 */
u32 mmc3::GetTileLMA(const u16 tileVMA) {
    constexpr u32 k1K = 0x400;
    const u16 vma = tileVMA & 0x1FFF;
    // R0/R1 only: the register latches an even bank number -- see this
    // function's own doc comment.
    const u8 bank0 = banks[0] & ~1u;
    const u8 bank1 = banks[1] & ~1u;
    u32 lma;

    if (shape) {
        if      (vma < 0x0800) lma = bank0 * k1K + (vma - 0x0000);
        else if (vma < 0x1000) lma = bank1 * k1K + (vma - 0x0800);
        else if (vma < 0x1400) lma = banks[2] * k1K + (vma - 0x1000);
        else if (vma < 0x1800) lma = banks[3] * k1K + (vma - 0x1400);
        else if (vma < 0x1C00) lma = banks[4] * k1K + (vma - 0x1800);
        else                   lma = banks[5] * k1K + (vma - 0x1C00);
    } else {
        if      (vma < 0x0400) lma = banks[2] * k1K + (vma - 0x0000);
        else if (vma < 0x0800) lma = banks[3] * k1K + (vma - 0x0400);
        else if (vma < 0x0C00) lma = banks[4] * k1K + (vma - 0x0800);
        else if (vma < 0x1000) lma = banks[5] * k1K + (vma - 0x0C00);
        else if (vma < 0x1800) lma = bank0 * k1K + (vma - 0x1000);
        else                   lma = bank1 * k1K + (vma - 0x1800);
    }

    return lma % ppu::chrRomBytes;
}

/**
 * @brief Strong ::ppu::ResolveTile override -- see that function's own doc
 * comment (video.hpp) for the weak/strong relationship. Applies to every
 * MMC3 board, four-screen or not: CHR bank switching is independent of
 * nametable wiring.
 */
u32 ppu::ResolveTile(const u16 tileVMA) {
    return mmc3::GetTileLMA(tileVMA);
}

#if ALTERNATIVE_NAMETABLE == 1
/**
 * @brief Four-screen nametable/attribute VRAM routing -- strong overrides of
 * ::ppu::InitCartVRAM/::ppu::nametableRows/::ppu::ReadNametable/
 * ::ppu::WriteNametable (see each function's own doc comment, video.hpp),
 * only compiled in when this board build carries the extra 2 KiB cartridge
 * VRAM chip (mmc3.hpp's own comment on ALTERNATIVE_NAMETABLE). A board built
 * without it never defines these symbols at all, so the emu PPU's weak
 * defaults (plain ::VideoRAM access, ::nametableRows == 1) stand unchanged --
 * see mmc3.hpp's own comment on why that's also correct for MMC3's ordinary
 * runtime H/V mirroring switch (::SetMirroring), which a real four-screen
 * board's $A000 write is simply meaningless against (each quadrant already
 * has fixed, dedicated storage), so nothing needs to special-case that call.
 */

u8*      mmc3::cartVRAM         = nullptr;
unsigned mmc3::cartVRAMRowBytes = 0;

/**
 * @brief Allocates ::cartVRAM, sized via ::video::nametable_row_bytes() --
 * NOT the @p vram_bytes ::emu::InitMemory happened to allocate ::VideoRAM
 * with (see that function's own doc comment, video.hpp, for why those two
 * can differ). Zeroed (calloc, not malloc): unlike ::VideoRAM, nothing ever
 * writes default content into this row (::ppu::Flush only ever touches row
 * 0), and the demo's own scroll writes can legitimately carry the render
 * walk across the row-0/row-1 boundary well before any game populates row 1
 * -- e.g. a mid-frame HUD-split scroll write that starts a fixed few rows
 * above the bottom of the screen crosses it on every frame, exactly as a
 * real four-screen board's own coarse-Y-wrap-toggles-nametable-select
 * hardware behavior would. calloc keeps that always-real crossing showing a
 * deterministic blank tile instead of whatever this process's heap
 * allocator happened to leave there. Never freed: lives for the process's
 * whole run, same as ::VideoRAM itself (see ::emu::InitMemory).
 */
void ppu::InitCartVRAM() {
    mmc3::cartVRAMRowBytes = video::nametable_row_bytes();
    mmc3::cartVRAM         = static_cast<u8 *>(calloc(mmc3::cartVRAMRowBytes, 1));
}

extern const u8 ppu::nametableRows = 2;

/**
 * @brief @p logical below ::cartVRAMRowBytes answers from ::VideoRAM (row 0,
 * unchanged from the weak default); at or above it answers from ::cartVRAM
 * (row 1), offset back down to a 0-based index into that buffer. A stored
 * byte-length comparison, not a fixed bitmask against @p logical: the cart
 * row always mirrors row 0's own (viewport-dependent) page count, so this
 * stays correct regardless of how many horizontal pages a given viewport
 * needs.
 */
u8 ppu::ReadNametable(const u16 logical) {
    return logical < mmc3::cartVRAMRowBytes
        ? VideoRAM[logical]
        : mmc3::cartVRAM[logical - mmc3::cartVRAMRowBytes];
}

void ppu::WriteNametable(const u16 logical, const u8 value) {
    if (logical < mmc3::cartVRAMRowBytes) VideoRAM[logical] = value;
    else mmc3::cartVRAM[logical - mmc3::cartVRAMRowBytes] = value;
}

/**
 * @brief Strong override of ::ppu::Flush for the four-screen board.
 *
 * The weak default (src/emu/ppu.cpp) only walks ::video::nametable_row_bytes()
 * worth of pages -- "row 0" (::VideoRAM), correct for an ordinary mirrored
 * board where 2 physical pages alias to cover all 4 logical nametables. A
 * four-screen board has no such mirroring: row 1 (::mmc3::cartVRAM) is
 * genuinely separate physical storage and never gets touched by the weak
 * default at all, so anything placed there (e.g. this demo's title/menu
 * text, which ::title.cpp's own comment says deliberately lands in the
 * "bottom-right" quadrant -- nt_v==1, row 1) keeps whatever attribute bytes
 * ::ppu::InitCartVRAM's calloc left (0x00, palette 0) instead of the
 * palette ::Flush's caller asked for. Same page-order convention as this
 * file's own logical addressing (page = nt_h + nt_v*nt_cols, see
 * xy_to_nt_addr, src/emu/ppu.cpp) and matches the real NES-side strong
 * override (src/nes/mappers/mmc3.cpp) walking every physical page instead
 * of relying on mirroring.
 */
void ppu::Flush(const u8 nt, const u8 at) {
    const unsigned vpw     = video::viewport_px();
    const u16      nt_cols = static_cast<u16>(vpw < 512 ? 2 : (vpw + 255) / 256);
    const u16      pages   = static_cast<u16>(nt_cols * ppu::nametableRows);

    for (u16 page = 0; page < pages; page++) {
        for (u16 i = 0; i < 0x3c0; i++) {
            ppu::WriteNametable(static_cast<u16>(page * 0x400 + i), nt);
        }
        for (u16 i = 0; i < 0x40; i++) {
            ppu::WriteNametable(static_cast<u16>(page * 0x400 + 0x3c0 + i), at);
        }
    }
}
#endif
