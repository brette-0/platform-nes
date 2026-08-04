/*
 * NES-MMC3 (mapper 4) linker helper script -- LIBRARY-OWNED.
 *
 * Real MMC3 hardware addressing (PRG mode 0, CHR mode 0 -- the only
 * combination mmc3.hpp/mmc3.cpp support; see mmc3.hpp's file comment):
 *   $8000-$9FFF  8 KiB PRG-ROM, switchable  (R6, via $8000/$8001)
 *   $A000-$BFFF  8 KiB PRG-ROM, switchable  (R7, via $8000/$8001)
 *   $C000-$DFFF  8 KiB PRG-ROM, FIXED to the second-to-last physical bank
 *   $E000-$FFFF  8 KiB PRG-ROM, FIXED to the last physical bank
 *   $6000-$7FFF  up to 8 KiB PRG-RAM ("cartridge memory", see mmc3.hpp's
 *                ::cartmem)
 *
 * Mirrors vrc1-helper.ld's own split as closely as MMC3's hardware allows:
 * this file owns only genuinely mechanical/hardware MMC3 facts (window
 * geometry, the two fixed banks, the reset vector, PRG-RAM's address). A
 * consuming project's own top-level script still owns __prg_rom_size/
 * __chr_rom_size and any of its own MMC3_BANKED()-tagged domains -- see
 * vrc1-helper.ld's own file comment and BANKED_CALL_THEORY.txt's "THE FINAL
 * LINKER SCRIPT MUST BE USER-MADE" section for the reasoning this follows.
 * (See demo/link.ld for a worked example of that split for VRC1; an MMC3
 * project's own top-level script would follow the same shape, `INCLUDE
 * mmc3-helper.ld` in place of `INCLUDE vrc1-helper.ld`.)
 *
 * UNLIKE VRC1's three independently-selectable PRG windows, MMC3 in PRG
 * mode 0 only exposes TWO registers a project can point anywhere ($8000 via
 * R6, $A000 via R7). $C000-$DFFF is hardware-fixed to the second-to-last
 * bank regardless of any register write -- not a third switchable window.
 *
 * CAUGHT DURING THE FIRST REAL CONSUMER (this project's own demo, converting
 * from VRC1): prg_rom_switchable used to span all of $8000-$DFFF as ONE
 * region for ordinary/untagged code, copying VRC1's own three-equal-windows
 * shape directly. That's only actually safe at EXACTLY 4 total PRG-ROM
 * banks: "second-to-last" only equals "the third 8 KiB chunk in file order"
 * when there are exactly 4 banks to begin with. The moment a project needs
 * a 5th+ bank for anything (an explicit MMC3_BANKED() domain, e.g.), ordinary
 * code the linker placed in that third chunk stops being what $C000 actually
 * shows at boot -- second-to-last has shifted to a later physical bank, and
 * that "third chunk" content becomes silently unreachable (no register can
 * select it; it isn't $8000's or $A000's bank, and $C000 no longer shows it
 * either). prg_rom_switchable below is therefore only $8000-$BFFF (16 KiB,
 * R6/R7's own two windows) -- the one span whose contents are ALWAYS exactly
 * what boots up, regardless of total bank count. $C000-$DFFF is deliberately
 * left out of c_readonly's target entirely: its content is real and
 * addressable (via R6/R7's explicit-bank path, same as any other bank -- see
 * mmc3.hpp's detail::CallInSection), but nothing should rely on it being
 * "whatever ordinary code overflowed into," since that's only true for a
 * 4-bank ROM. A consuming project that DOES know its own exact bank count
 * and wants to deliberately use the second-to-last slot may still declare
 * its own MEMORY region for it (ORIGIN 0xc000, no bank-encoding needed since
 * it's never reached via register anyway) -- this file just doesn't do that
 * on a project's behalf, the same reasoning vrc1-helper.ld's own file
 * comment gives for not making project-specific domain decisions here.
 *
 * $6000-$7FFF (PRG-RAM, "cartridge memory") is genuinely optional on real
 * MMC3 boards, but this file always reserves it (size driven by
 * __program_ram_size/__program_nvram_size, PROVIDE()'d below with a
 * working default) so ::cartmem has real backing memory with no extra
 * consumer configuration required -- override either before this INCLUDE
 * (or to 0) to change that.
 *
 * DELIBERATELY NOT __prg_ram_size/__prg_nvram_size (the SDK's own canonical
 * names, used by e.g. nes-mmc3/nes-mmc1's own common.ld and by
 * ines-header.ld): confirmed the generic "nes" platform's own crt0.o
 * already contains a WEAK object-file definition of those two names (both
 * 0; `llvm-nm crt0.o` shows `00000000 W __prg_ram_size`), and a weak symbol
 * still counts as "defined" as far as PROVIDE() is concerned -- it silently
 * lost to crt0.o's weak 0 in a real link, collapsing prg_ram's own LENGTH
 * below to 0. Rather than fight that with a plain (non-PROVIDE) assignment,
 * this file uses its own distinct names instead: nothing else defines
 * __program_ram_size, so PROVIDE() behaves exactly as advertised. The only
 * real cost is that the canonical __prg_ram_size stays whatever crt0.o
 * defaults it to (0) -- harmless here, since this project doesn't use
 * ines-header.ld at all (see vrc1-helper.ld's own NOTE ON __mapper comment)
 * and nes.ld's only use of it is a power-of-2 ASSERT that 0 trivially
 * satisfies regardless of this file's real PRG-RAM size.
 *
 * c_writeable/__stack are NOT set here at all -- not even as an inlined
 * default the way an earlier revision of this file did (replacing
 * `INCLUDE c-in-ram.ld` with its own two equivalent lines). Confirmed
 * directly against this project's own build that a REGION_ALIAS, unlike a
 * plain assignment, cannot be overridden by a consuming project afterward
 * (ld.lld rejects a second alias for the same name outright) -- so baking
 * ANY default in here would permanently commit every consumer to whichever
 * region this file picked, with no escape short of forking it. See the
 * comment right above the (now absent) REGION_ALIAS/__stack lines, further
 * down this file, for what a consuming project should write instead.
 *
 * No nes-no-pow2.ld-style fork of nes.ld exists here: that fork exists for
 * a VRC1-specific, already-hit need (a genuinely non-power-of-2 PRG-ROM
 * size); nothing about MMC3 needs the same relief, so this file INCLUDEs
 * the SDK's own nes.ld unmodified, power-of-2 ASSERT intact.
 */

INCLUDE nes.ld

PROVIDE(__program_ram_size = 8);
PROVIDE(__program_nvram_size = 0);

ASSERT(__program_ram_size + __program_nvram_size <= 8,
       "MMC3 only supports up to 8 KiB of PRG-(NV)RAM.")

/* c_writeable/__stack are DELIBERATELY NOT set here, unlike an earlier
 * revision of this file -- same reasoning as the project's own established
 * "you don't need c-in-ram.ld, implement it yourself" conclusion (this
 * file's own two lines were themselves already that same trick, replacing
 * the SDK's c-in-ram.ld): where the C writable arena (.data/.bss/heap, and
 * the dynamic stack that shares it -- see this project's own memory on
 * llvm-mos soft stacks) lives is project policy, not an MMC3 hardware fact,
 * and REGION_ALIAS specifically CANNOT be overridden after the fact the way
 * __stack alone can (confirmed empirically: ld.lld rejects a second
 * REGION_ALIAS for the same name outright, "redefinition of memory
 * region" -- unlike a plain assignment, where the last one processed simply
 * wins). Baking a default in here would therefore permanently commit every
 * consumer to system RAM, with no way to move it to PRG-RAM afterward
 * (exactly what this project's own demo/link.ld now does) short of forking
 * this file. A consuming project must declare its own
 * `REGION_ALIAS("c_writeable", <region>)` and `__stack = <address>;` AFTER
 * `INCLUDE mmc3-helper.ld` (the referenced region -- ram or prg_ram, both
 * declared below -- must already exist by then) -- see demo/link.ld for a
 * worked example targeting prg_ram instead of ram.
 */

/* How many 8 KiB windows starting at $8000 count as "ordinary resident
 * space" (prg_rom_switchable's own LENGTH, below) -- 2 by default (R6/R7's
 * own two windows only, $8000-$BFFF), the one value that's safe regardless
 * of total PRG-ROM size (see prg_rom_switchable's own comment for why 3
 * isn't, in general). A consuming project that commits to an EXACT total
 * bank count of 4 (so that "second-to-last" really is the third chunk in
 * file order) may safely reclaim that third window as ordinary space too,
 * by PROVIDE()-ing 3 here BEFORE `INCLUDE mmc3-helper.ld` -- same
 * before-the-INCLUDE ordering PROVIDE() always needs to win (confirmed
 * empirically: a REGION_ALIAS cannot simply be re-aliased afterward the way
 * a plain symbol assignment like __stack can; ld.lld rejects a second
 * REGION_ALIAS for the same name outright, "redefinition of memory
 * region"). See demo/link.ld for a worked example of a project doing
 * exactly this. Never 1 or 0: R6's own window ($8000-$9FFF) is always
 * ordinary/resident space here regardless. */
PROVIDE(__mmc3_resident_windows = 2);

MEMORY {
  /* $8000-$BFFF by default (16 KiB, windows 1+2 only -- registers R6/R7,
   * mmc3.hpp's window1Control/window2Control); a consuming project that
   * commits to an exact 4-bank total may widen this to $8000-$DFFF (24 KiB)
   * via __mmc3_resident_windows (see its own comment just above). Ordinary
   * untagged code/data lives here -- see this file's own header comment for
   * why $C000-$DFFF is NOT part of this region by default. */
  prg_rom_switchable : ORIGIN = 0x8000, LENGTH = __mmc3_resident_windows * 0x2000
  /* Hardwired to the LAST physical bank, unconditionally, regardless of any
   * register -- the one truly permanent, always-resident home, matching
   * VRC1's own $E000-$FFFF exactly. .prg_rom_fixed-tagged content only. */
  prg_rom_fixed       : ORIGIN = 0xe000, LENGTH = 0x2000
  /* $6000-$7FFF: PRG-RAM ("cartridge memory", mmc3.hpp's ::cartmem). See
   * this file's own header comment on __program_ram_size/__program_nvram_size. */
  prg_ram             : ORIGIN = 0x6000, LENGTH = (__program_ram_size + __program_nvram_size) * 1024
  /* The 16-byte NES2.0 header -- same bookkeeping-only region VRC1 uses,
   * never CPU/PPU-addressed. Filled by header.hpp's NES2_HEADER() macro. */
  nes2_header         : ORIGIN = 0x02000000, LENGTH = 16
}

REGION_ALIAS("c_readonly", prg_rom_switchable)

SECTIONS {
  .prg_rom_fixed : { *(.prg_rom_fixed .prg_rom_fixed.*) } >prg_rom_fixed
  /* mmc3.hpp's ::cartmem / ::sysmem -- explicit memory positioning.
   * NOLOAD: runtime-only storage, nothing to load from the ROM file. */
  .cartmem (NOLOAD) : { *(.cartmem .cartmem.*) } >prg_ram
  .sysmem  (NOLOAD) : { *(.sysmem .sysmem.*) } >ram
  .nes2_header   : { KEEP(*(.nes2_header)) } >nes2_header
}

/* _nes2_header (see header.hpp's NES2_HEADER()) is otherwise never
 * referenced by any code, so without this the linker would never pull that
 * object out of libplatform-nes.a -- same reasoning as vrc1-helper.ld's
 * own EXTERN(_nes2_header). */
EXTERN(_nes2_header)

INCLUDE rompoke-table.ld
INCLUDE dpcm.ld

/* Falls back to _start when nothing provides a real _reset (e.g. CMake's
 * own internal compiler-identification/ABI probes) -- same reasoning as
 * vrc1-helper.ld's own PROVIDE(_reset = _start). */
PROVIDE(_reset = _start);

SECTIONS {
  .vector 0xfffa : { SHORT(nmi) SHORT(_reset) SHORT(irq) } >prg_rom_fixed
}

/* Only the two regions this file itself owns -- prg_rom_fixed and chr_rom
 * are deliberately NOT FULL()'d here, matching vrc1-helper.ld's own split
 * exactly: prg_rom_fixed must stay the physically LAST content in the ROM
 * file (real MMC3 hardware always shows the true last bank there), so a
 * consuming project's own top-level script has to list any of its own
 * MMC3_BANKED()-tagged domains' FULL() calls FIRST, then re-list
 * FULL(prg_rom_fixed)/FULL(chr_rom) itself, last -- see demo/link.ld's own
 * OUTPUT_FORMAT for the worked VRC1 example of this same ordering
 * requirement. Baking those two in here instead would force them before
 * anything a consumer adds afterward, silently breaking that guarantee. */
OUTPUT_FORMAT {
  FULL(nes2_header)
  FULL(prg_rom_switchable)
}
