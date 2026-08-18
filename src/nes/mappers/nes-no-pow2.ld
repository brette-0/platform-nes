/*
 * Fork of mos-platform/nes/lib/nes.ld (llvm-mos SDK), with exactly one
 * change: the PRG-ROM size ASSERT below is REMOVED.
 *
 * WHY THIS FORK EXISTS: the upstream ASSERT rejects any PRG-ROM size that
 * isn't an exact power of 2 -- e.g. 40 KiB (5 real 8 KiB banks), which
 * BANKED_CALL_THEORY.txt's Phase 3 genuinely needs and which NES2.0's own
 * header format can represent exactly (exponent-multiplier form, odd part
 * 5 -- see nes2.hpp's rom_size_representable()). Investigated where this
 * requirement actually comes from before forking anything:
 *
 * - It is NOT enforced by ld.lld itself -- confirmed by grepping the linker
 *   binary's own strings for the error text and finding nothing. It lives
 *   entirely in this plain ASSERT, ordinary linker-script text, the exact
 *   same mechanism this project's own vrc1.ld already uses for its own
 *   checks.
 * - It is STRICTER than what llvm-mos's own header generator
 *   (ines-header.ld) actually needs. That file computes the PRG-ROM header
 *   byte as `__prg_rom_size / 16` -- a PLAIN count of 16 KiB units, not an
 *   exponent-multiplier encoding (no NES2.0 byte-9 0xF nibble is ever set).
 *   Plain notation only actually requires PRG-ROM size to be a MULTIPLE OF
 *   16 KiB (e.g. 48 KiB would be fine for ines-header.ld -- 3 units -- and
 *   ISN'T a power of 2), not a strict power of 2. The blanket power-of-2
 *   ASSERT is a defensive simplification protecting that one generator's
 *   simpler encoding path, not a real hardware or toolchain requirement.
 * - This project doesn't use ines-header.ld at all (see vrc1-helper.ld's own
 *   file comment) -- header.hpp/nes2.hpp builds the NES2.0 header directly,
 *   already supporting the full exponent-multiplier form (odd part 1/3/5/7
 *   times a power of 2), which represents 40 KiB exactly, correctly.
 * - Neither mapper's hardware has a power-of-2 requirement: VRC1's PRG
 *   select registers are plain 4-bit bank numbers written directly (0-15);
 *   MMC3's R6/R7 are plain 6-bit bank numbers (0-63). Both simply never
 *   write an unused value on a non-power-of-2 ROM, same as any other bank.
 *
 * Net: nothing this project actually uses depends on PRG-ROM being a power
 * of 2, for either mapper -- both vrc1-helper.ld and mmc3-helper.ld INCLUDE
 * this file instead of the SDK's own nes.ld. Everything else below is
 * unmodified from upstream nes.ld -- keep it that way; re-diff against a
 * fresh SDK install if llvm-mos updates this file, rather than hand-merging
 * changes blind.
 */

/* Provide imaginary (zero page) registers. */
__rc0 = 0x00;
INCLUDE imag-regs.ld
ASSERT(__rc31 == 0x1f, "Inconsistent zero page map.")

ASSERT(__chr_rom_size == 0 || __chr_rom_size == 1 << LOG2CEIL(__chr_rom_size),
       "CHR-ROM size must be a power of 2.")
ASSERT(__prg_ram_size == 0 ||
       __prg_ram_size == 1 << LOG2CEIL(__prg_ram_size),
       "Nonempty PRG-RAM must be a power of 2.")
ASSERT(__prg_nvram_size == 0 ||
       __prg_nvram_size == 1 << LOG2CEIL(__prg_nvram_size),
       "Nonempty PRG-NVRAM must be a power of 2.")
ASSERT(__chr_ram_size == 0 ||
       __chr_ram_size == 1 << LOG2CEIL(__chr_ram_size),
       "Nonempty CHR-RAM must be a power of 2.")
ASSERT(__chr_nvram_size == 0 ||
       __chr_nvram_size == 1 << LOG2CEIL(__chr_nvram_size),
       "Nonempty CHR-NVRAM must be a power of 2.")

MEMORY {
  zp : ORIGIN = __rc31 + 1, LENGTH = 0x100 - (__rc31 + 1)

  /* CPU address space. */
  ram : ORIGIN = 0x0200, LENGTH = 0x0600

  chr_rom : ORIGIN = 0x01000000, LENGTH = __chr_rom_size * 1024
}

SECTIONS {
  .text : {
       INCLUDE text-sections.ld
       KEEP(*(.nmi_begin))
       KEEP(*(SORT_BY_INIT_PRIORITY(.nmi.* .nmi)))
       KEEP(*(.nmi_end))
  } >c_readonly
  INCLUDE rodata.ld
  /* A naturally page-aligned place (0x200) to place noinit buffers with high
   * alignment requirement (e.g. OAM) */
  .aligned (NOLOAD) : { *(.aligned .aligned.*) } >ram
  INCLUDE data.ld
  INCLUDE zp.ld
  INCLUDE bss.ld
  INCLUDE noinit.ld

  .ram (NOLOAD) : { *(.ram .ram.*) } > ram

  .chr_rom : { KEEP(*(.chr_rom .chr_rom.*)) } >chr_rom
}
