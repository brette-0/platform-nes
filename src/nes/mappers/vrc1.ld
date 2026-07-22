/*
 * NES-VRC1 (mapper 75) NES2.0 file linker script.
 *
 * Real VRC1 hardware addressing:
 *   $8000-$9FFF  8 KiB PRG-ROM, switchable  (PRG Select 0, write $8000-$8FFF)
 *   $A000-$BFFF  8 KiB PRG-ROM, switchable  (PRG Select 1, write $A000-$AFFF)
 *   $C000-$DFFF  8 KiB PRG-ROM, switchable  (PRG Select 2, write $C000-$CFFF)
 *   $E000-$FFFF  8 KiB PRG-ROM, FIXED to the last physical bank
 *
 * PRG-ROM is split into two real, address-bound MEMORY regions:
 * prg_rom_switchable ($8000-$DFFF, 24 KiB -- windows 1-3) and prg_rom_fixed
 * ($E000-$FFFF, 8 KiB -- hardwired). Content explicitly tagged
 * .prg_rom_fixed (see ::fixed in vrc1.hpp) is guaranteed by the linker to
 * fit inside that 8 KiB, erroring out on overflow rather than silently
 * landing wherever ordinary switchable content happens to end -- which is
 * what an earlier version of this script did by aliasing prg_rom_fixed onto
 * the *whole* flat $8000-$FFFF region, and which broke the reset vector
 * (see below). Ordinary untagged code/data goes into prg_rom_switchable.
 *
 * At the project's current PRG-ROM size (32 KiB = exactly 4 banks), each
 * switchable window has only one physical bank it could ever show once
 * boot-time PRG Select 0/1/2 initialization (see _reset, below) has run --
 * so every existing call still behaves like an ordinary same-bank call with
 * zero source changes, while the physical ROM image, NES2.0 header (mapper
 * 75) and per-window addressing are genuine VRC1.
 *
 * RESET VECTOR: real VRC1 hardware powers up with the PRG-select registers
 * in an undefined state, so at cold boot the switchable windows ($8000-
 * $DFFF) can show ANY physical bank -- not necessarily the one this
 * script's flat layout assumes goes there. Only the fixed window ($E000-
 * $FFFF) is guaranteed correct at power-on. crt0's own entry point, _start
 * (see mos-platform/common/lib/text-sections.ld: `_start = .;`, the very
 * first byte of ordinary .text), therefore lands in the switchable region
 * and is NOT safe to jump to directly from reset -- nor is any of crt0's
 * own startup code that runs before main, since that's ordinary .text too.
 * The reset vector below instead targets _reset (defined in vrc1.cpp,
 * tagged ::fixed): a minimal stub that performs the boot-time PRG Select
 * 0/1/2 initialization *before* anything in the switchable region has
 * executed, then falls through into _start once doing so is actually safe.
 * This mirrors the _reset/.reset pattern nes-mmc3's own linker scripts use
 * for exactly the same reason (see mos-platform/nes-mmc3/lib/_reset.ld and
 * reset-banked-8.o, which does the equivalent for MMC3's own bank-select
 * registers).
 *
 * PRECONDITION: __mapper is deliberately left to this script (set below),
 * not to crt0. nes-nrom's own crt0.o hardcodes __mapper = 0 as a non-weak
 * symbol, so linking this script against that particular crt0.o will fail
 * with a multiple-definition error on __mapper. This script must be linked
 * against a crt0 that leaves __mapper undefined (e.g. the generic nes
 * platform's crt0.o, not nes-nrom's).
 *
 * COROLLARY: that same crt0 swap also drops nes-nrom crt0.o's weak
 * __prg_rom_size/__chr_rom_size defaults (32/8) -- it's the only crt0 that
 * happens to define them, as a side effect of being NROM-specific. The
 * generic nes platform's crt0.o leaves both undefined, so this script must
 * PROVIDE its own defaults below (matching nes-nrom's values, since that's
 * the layout this script's MEMORY blocks assume) or linking fails with
 * "symbol not found: __prg_rom_size".
 *
 * Growing PRG-ROM past 32 KiB (adding banks 4+) is future work: those extra
 * banks only become reachable through explicit .prg_rom_0 / .prg_rom_1 /
 * .prg_rom_2 section tags, once real runtime bank-switching exists.
 */

INCLUDE nes.ld
INCLUDE c-in-ram.ld

__mapper = 75;

PROVIDE(__prg_rom_size = 32);
PROVIDE(__chr_rom_size = 8);

ASSERT(__prg_rom_size == 32,
       "This VRC1 script currently only supports the project's existing 32 KiB PRG-ROM (4 banks); growing past that needs explicit .prg_rom_N tagging -- see the comment at the top of this file.")

ASSERT(__chr_rom_size <= 8,
       "This VRC1 script does not wire up CHR banking; only the existing flat 8 KiB of CHR-ROM is supported.")

MEMORY {
  /* Windows 1-3: $8000-$DFFF. Ordinary untagged code/data lives here. */
  prg_rom_switchable : ORIGIN = 0x8000, LENGTH = 0x6000
  /* Hardwired to the last physical bank on real hardware, regardless of any
   * PRG-select register's value. .prg_rom_fixed-tagged content only. */
  prg_rom_fixed       : ORIGIN = 0xe000, LENGTH = 0x2000
  /* The 16-byte NES2.0 header prefixed to the ROM file itself -- never CPU-
   * or PPU-addressed, so (like nes.ld's chr_rom region at 0x01000000) the
   * origin is a fake address that exists only for FULL()'s bookkeeping
   * below. Filled by header.hpp's NES2_HEADER() macro (see nes2.cpp). */
  nes2_header         : ORIGIN = 0x02000000, LENGTH = 16
}

REGION_ALIAS("c_readonly", prg_rom_switchable)

SECTIONS {
  .prg_rom_fixed : { *(.prg_rom_fixed .prg_rom_fixed.*) } >prg_rom_fixed
  .nes2_header   : { KEEP(*(.nes2_header)) } >nes2_header
}

/* _nes2_header (see header.hpp's NES2_HEADER()) is otherwise never referenced
 * by any code -- nothing in the executable calls into nes2.cpp -- so without
 * this the linker would never pull that object out of libplatform-nes.a and
 * .nes2_header above would stay empty (silently zero-padded by FULL(), below).
 * Naming the symbol here forces extraction, exactly like the .vector table's
 * direct SHORT(nmi)/SHORT(_reset)/SHORT(irq) references do for those objects. */
EXTERN(_nes2_header)

INCLUDE rompoke-table.ld
INCLUDE dpcm.ld

/* Falls back to _start when nothing provides a real _reset (e.g. CMake's
 * own internal compiler-identification/ABI probes, which link a bare
 * `int main(){}` scratch file with no vrc1.cpp in sight) -- PROVIDE only
 * takes effect when the symbol is otherwise undefined, so vrc1.cpp's real
 * _reset always wins in an actual project build. Without this, any build
 * that doesn't happen to link vrc1.cpp fails to link at all, even for an
 * unrelated trivial test program -- this bit real, well before it ever got
 * near application code. */
PROVIDE(_reset = _start);

SECTIONS {
  .vector 0xfffa : { SHORT(nmi) SHORT(_reset) SHORT(irq) } >prg_rom_fixed
}

OUTPUT_FORMAT {
  FULL(nes2_header)
  FULL(prg_rom_switchable)
  FULL(prg_rom_fixed)
  FULL(chr_rom)
}
