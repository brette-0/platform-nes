/*
 * Demo's own top-level linker script -- CONSUMER-OWNED, not library code.
 *
 * Converted from VRC1 to MMC3: INCLUDEs mmc3-helper.ld instead of
 * vrc1-helper.ld for the hardware/mechanical facts. The previous VRC1-only
 * Phase 2-4 smoke-test domains (prg_rom_window_test, prg_rom_bank3,
 * prg_rom_oversized) are gone entirely: those existed purely to exercise
 * vrc1.cpp's own internal BANKED_CALL machinery, which this build no longer
 * links (see CMakeLists.txt's own NES_SOURCES exclusion of vrc1.cpp) --
 * they were never part of this demo's actual application code.
 *
 * FamiStudio's own dedicated bank (Phase 6 in the VRC1 version) is ALSO
 * gone, for a real reason rather than just less to port: this demo has no
 * frequent cross-bank calling and no interrupt-driven bank switching (see
 * this repo's own README/commit history), so there is no actual need to
 * pay for MMC3_BANKED()'s farcall machinery just to reach FamiStudio.
 * FamiStudio's compiled code now lives as ordinary, always-resident content
 * in the SAME flat image as the rest of the demo (prg_rom_switchable,
 * widened below) -- audio.cpp calls its entry points directly, no
 * mmc3::Call<Fn>/mmc3::CallBlock<Fn>/MMC3_BANKED_EXTERN wrapper needed at
 * all.
 */

/* This demo's own ROM size -- NOT an MMC3 fact, purely this project's own
 * choice, which is exactly why it doesn't live in mmc3-helper.ld. 32 KiB,
 * 4 banks -- unchanged from this demo's original pre-BANKED_CALL VRC1 size.
 * Every ASSERT below depends on this being EXACTLY 4: see
 * __mmc3_resident_windows just below for why. */
PROVIDE(__prg_rom_size = 32);
PROVIDE(__chr_rom_size = 8);

/* mmc3-helper.ld's own prg_rom_switchable is deliberately only 16 KiB
 * ($8000-$BFFF, R6/R7's own two windows) by default -- see that file's own
 * comment on __mmc3_resident_windows for why: $C000-$DFFF's "second-to-last
 * bank" identity SHIFTS as PRG-ROM grows, so it isn't safe to treat as
 * ordinary resident space IN GENERAL. This demo can safely widen it anyway,
 * because it commits (via the ASSERT below) to EXACTLY 4 total banks -- at
 * exactly 4, "second-to-last" IS the third 8 KiB chunk in file order, no
 * different from banks 0 and 1 landing at R6/R7's own defaults. 3 windows
 * -> $8000-$DFFF, 24 KiB total -- exactly the flat, three-window-equivalent
 * image this demo used under VRC1. MUST come BEFORE `INCLUDE mmc3-helper.ld`
 * below: PROVIDE() only takes its FIRST-encountered value, and
 * mmc3-helper.ld provides its own default (2) for this same symbol -- same
 * ordering rule __prg_rom_switchable_reserved's own history (VRC1's
 * demo/link.ld) already established. If this demo ever needs a 5th+ bank
 * (an explicit MMC3_BANKED()-tagged domain), this has to be revisited
 * FIRST -- it would silently place content that becomes unreachable the
 * moment total bank count moves off exactly 4. */
PROVIDE(__mmc3_resident_windows = 3);

INCLUDE mmc3-helper.ld

/* Moves the C writable arena (.data/.bss/heap, and the dynamic soft stack
 * that shares it) into PRG-RAM ("cartridge memory") instead of mmc3-helper.ld's
 * absent default (system RAM) -- see this file's own comment on why
 * mmc3-helper.ld leaves c_writeable/__stack entirely unset. Frees the
 * console's own onboard RAM ($0200-$07FF, only 1.5 KiB total) for other use
 * (OAM buffers, hot always-resident state) instead of sharing it with
 * whatever the soft stack's worst-case depth needs -- see this project's own
 * memory on llvm-mos soft stacks for why that competition is a real, easy-
 * to-hit concern on a 2 KiB-RAM target, and why it's worth moving off system
 * RAM entirely now that PRG-RAM gives 8 KiB of headroom instead of fighting
 * over scraps of 1.5 KiB.
 *
 * MUST come AFTER `INCLUDE mmc3-helper.ld`, not before: prg_ram is declared
 * inside that file's own MEMORY block, so REGION_ALIAS has nothing to
 * reference until the INCLUDE has actually run.
 *
 * __stack = ORIGIN(prg_ram) + LENGTH(prg_ram) - 1: the same convention the
 * SDK's own nes/lib/c-in-prg-ram-fixed.ld uses for a PRG-RAM-resident stack
 * (confirmed against that file directly, not guessed) -- the top BYTE of
 * the region, not one past the end. At this project's __program_ram_size
 * (8 KiB, mmc3-helper.ld's own default, unchanged here), that's $7FFF. */
REGION_ALIAS("c_writeable", prg_ram)
__stack = ORIGIN(prg_ram) + LENGTH(prg_ram) - 1;

ASSERT(__prg_rom_size == 32,
       "This demo currently only supports its existing 32 KiB PRG-ROM (4 banks) -- growing past that needs revisiting __mmc3_resident_windows' own 4-bank-only assumption above before just bumping this number.")

SECTIONS {
  /* FamiStudio's compiled code (FSCODE, no leading dot -- see
   * vrc1-helper.ld's own file comment on ld.lld's XO65 underscore-escape
   * reader, confirmed empirically there and unchanged by this conversion)
   * is ordinary, always-resident content now -- routed into the SAME flat
   * image as everything else (prg_rom_switchable, widened to 24 KiB above),
   * not a dedicated bank. FSCODE MUST match CMakeLists.txt's
   * FAMISTUDIO_SEGMENT cache variable's value literally; the ASSERT below
   * is the safety net for the two drifting apart. */
  FSCODE : { *(FSCODE) } >prg_rom_switchable
}

/* Regression guard for FSCODE actually capturing FamiStudio's real content
 * -- SIZEOF() being nonzero is the only way to tell from in here. Zero IS
 * tolerated via DEFINED(nmiHandler): CMake's own internal compiler-
 * identification/ABI probes link a bare `int main(){}` with no
 * audio.cpp/famistudio_pass1.o in sight (confirmed with a real build, same
 * class of bare-probe case the VRC1 demo's own Phase 6 ASSERT existed for)
 * -- nmiHandler has C linkage (interrupts.hpp's ::interrupt macro) and
 * exists only in this demo's real main.cpp, so it's a stable, unmangled
 * stand-in for "is this actually the demo, not a probe." */
PROVIDE(__famistudio_domain_size = SIZEOF(FSCODE));
ASSERT(__famistudio_domain_size > 0 || !DEFINED(nmiHandler),
       "FSCODE captured no content -- CMakeLists.txt's FAMISTUDIO_SEGMENT cache variable no longer matches this file's own hardcoded FSCODE segment name (SECTIONS block above), so FamiStudio's real compiled code isn't landing in the ROM at all.")

/* prg_rom_switchable is already FULL()'d by mmc3-helper.ld's own
 * OUTPUT_FORMAT -- only prg_rom_fixed and chr_rom (deliberately NOT FULL()'d
 * there, see that file's own OUTPUT_FORMAT comment) are this project's job.
 * OUTPUT_FORMAT is additive across INCLUDEd files (confirmed empirically --
 * see vrc1-helper.ld's own OUTPUT_FORMAT comment) and every region name is
 * in one shared namespace by the time the whole script is assembled, so
 * this block's position, AFTER mmc3-helper.ld's own, is what actually
 * controls final file order (prg_rom_fixed must stay last among PRG-ROM
 * content, matching real MMC3 hardware always showing the true last bank
 * there). */
OUTPUT_FORMAT {
  FULL(prg_rom_fixed)
  FULL(chr_rom)
}
