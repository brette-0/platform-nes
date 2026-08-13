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
 * choice, which is exactly why it doesn't live in mmc3-helper.ld. 32 KiB
 * (4 banks), unchanged from this demo's original pre-BANKED_CALL VRC1 size.
 *
 * This is now a free knob: any power of 2 from 32 up to 512 KiB (MMC3's own
 * hardware maximum -- 6-bit bank registers) links correctly with no other
 * edit to this file, because mmc3-helper.ld anchors the always-resident
 * image to the TOP of the ROM file and pads the rest (see that file's own
 * header comment, and prg_rom_spare in the OUTPUT_FORMAT block below). It
 * previously could not move at all: the old layout put the resident image
 * at the BOTTOM of the file, which made "$C000 shows the second-to-last
 * bank" true only at exactly 4 banks, so this file carried an
 * ASSERT(__prg_rom_size == 32) to enforce that.
 *
 * TWO THINGS STILL TRACK THIS NUMBER BY HAND when it changes:
 * - demo/src/header.hpp's HEADER() prg_rom_bytes argument (32768) -- the
 *   NES2.0 header is built by the C++ side, which cannot see linker
 *   symbols, so a mismatch would ship a ROM whose header lies about its own
 *   size. See that file's own comment.
 * - Growing this does NOT grow the flat resident image ($8000-$DFFF, still
 *   24 KiB): the extra banks are reachable only through MMC3_BANKED()
 *   domains (plus __mmc3_reserved_banks, see mmc3-helper.ld). A c_readonly
 *   overflow is not fixed by bumping this number alone. */
PROVIDE(__prg_rom_size = 64);
PROVIDE(__chr_rom_size = 8);

/* Four banked domains, claiming EVERY bank the resident image doesn't use:
 * 64 KiB = 8 banks, of which 4 are resident (3 windows + the fixed bank), so
 * banks 0-3 are ours to name. Declaring all four leaves prg_rom_spare at
 * exactly 0 bytes -- no padding anywhere in this ROM. An unused domain costs
 * the same zeros the padding did, but is ready to fill without touching the
 * layout. MUST be set before `INCLUDE mmc3-helper.ld`: it is what shortens
 * prg_rom_spare, and PROVIDE() only takes its first-encountered value. */
PROVIDE(__mmc3_reserved_banks = 4);

INCLUDE mmc3-helper.ld

/* ------------------------------------------------------------------------
 * BANKED DOMAINS -- banks 0-3, 8 KiB each, all reached through window 1
 * ($8000-$9FFF, register R6).
 *
 * ORIGIN is ((bank + 1) * 0x10000) + 0x8000, and the +1 is load-bearing
 * TWICE over -- see mmc3.hpp's section_t ENCODING comment for the full
 * account, but in short: (a) the C++ side treats a zero high half as "no
 * bank encoded", so an unbiased bank 0 would be indistinguishable from
 * unset, and (b) an unbiased bank 0 region would sit at ORIGIN 0x8000,
 * overlapping this project's own .text, which ld.lld rejects outright
 * ("section .text virtual address range overlaps with .prg_rom_001" --
 * confirmed against a real link, not assumed). The high bits never reach the
 * emitted 6502 code: a JSR operand has 16 bits, so every one of these
 * truncates back to a plain $8000 call.
 *
 * A domain's REAL bank number is its file offset / 0x2000, which is decided
 * entirely by the FULL() order in the OUTPUT_FORMAT block at the bottom of
 * this file -- NOT by these ORIGINs, and NOT by the numbers in their names.
 * The two are kept in agreement by hand here (001 is FULL()'d first, so it
 * is bank 0, so its ORIGIN encodes 0+1), exactly the arithmetic that shipped
 * a real crash in the VRC1 rollout when it was got wrong -- see
 * BANKED_CALL_THEORY.txt's "CRITICAL POST-PHASE-4 FIX". Change one, change
 * all three: the FULL() order, the ORIGIN here, and the section() in
 * demo/src/banks.hpp.
 *
 * All four use window 1 rather than spreading across R6/R7 because nothing
 * in this demo yet needs two different domains banked in simultaneously; a
 * domain that does would encode 0xA000 instead and be reached through R7.
 * ------------------------------------------------------------------------ */
MEMORY {
  prg_rom_001 : ORIGIN = 0x00018000, LENGTH = 0x2000  /* bank 0 */
  prg_rom_002 : ORIGIN = 0x00028000, LENGTH = 0x2000  /* bank 1 */
  /* bank 2, but encoded for WINDOW 2 ($A000, R7) rather than window 1 --
   * note the A000 in the ORIGIN. This is the audio engine's DATA bank, and it
   * has to be mappable at the same time as the engine's CODE bank (002, R6):
   * the engine dereferences song pointers while it runs, and MMC3 has exactly
   * two switchable windows, so a same-window pair would have the data
   * immediately displace the code. mmc3::CallPairedBlock static_asserts that
   * the two tags differ here. */
  prg_rom_003 : ORIGIN = 0x0003a000, LENGTH = 0x2000  /* bank 2, window 2 */
  prg_rom_004 : ORIGIN = 0x00048000, LENGTH = 0x2000  /* bank 3 */
}

/* Published for the build-time space report -- see mmc3-helper.ld's own
 * __space_* block for why an empty bank needs these to be visible at all. */
__space_origin_prg_rom_001 = ORIGIN(prg_rom_001);
__space_length_prg_rom_001 = LENGTH(prg_rom_001);
__space_origin_prg_rom_002 = ORIGIN(prg_rom_002);
__space_length_prg_rom_002 = LENGTH(prg_rom_002);
__space_origin_prg_rom_003 = ORIGIN(prg_rom_003);
__space_length_prg_rom_003 = LENGTH(prg_rom_003);
__space_origin_prg_rom_004 = ORIGIN(prg_rom_004);
__space_length_prg_rom_004 = LENGTH(prg_rom_004);

SECTIONS {
  .prg_rom_001 : { *(.prg_rom_001 .prg_rom_001.*) } >prg_rom_001
  .prg_rom_002 : { *(.prg_rom_002 .prg_rom_002.*) } >prg_rom_002
  .prg_rom_003 : { *(.prg_rom_003 .prg_rom_003.*) } >prg_rom_003
  .prg_rom_004 : { *(.prg_rom_004 .prg_rom_004.*) } >prg_rom_004
}

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

/* Regression guard: the audio engine's own code really did land in its bank.
 * demo/famistudio_config.s's FAMISTUDIO_CA65_CODE_SEGMENT names the section
 * through ld.lld's XO65 escape encoding (_pprg__rom__002_fx -> .prg_rom_002),
 * and a typo there is otherwise silent -- the segment simply lands somewhere
 * else and the farcalls in src/nes/audio.cpp bank in an empty 8 KiB. There is
 * no FSCODE rule any more: naming the section after the BANK means the
 * .prg_rom_002 wildcard above already collects it, with no engine-specific
 * routing at all, which is what lets a non-FamiStudio engine drop in.
 *
 * Zero IS tolerated via DEFINED(nmi_handler), same as the FSCODE guard this
 * replaces: CMake's own compiler-identification/ABI probes link a bare
 * `int main(){}` with no audio in sight. */
PROVIDE(__audio_engine_size = SIZEOF(.prg_rom_002));
ASSERT(__audio_engine_size > 0 || !DEFINED(nmi_handler),
       "The audio engine's code segment landed nowhere -- demo/famistudio_config.s's FAMISTUDIO_CA65_CODE_SEGMENT no longer decodes to .prg_rom_002 (ld.lld XO65 escapes: _p is '.', __ is '_'), so audio.cpp's farcalls will bank in an empty region.")

/* mmc3-helper.ld FULL()s only nes2_header; every PRG region is this
 * project's job, in this exact order (see that file's own OUTPUT_FORMAT
 * comment for why it cannot emit them itself). OUTPUT_FORMAT is additive
 * across INCLUDEd files (confirmed empirically -- see vrc1-helper.ld's own
 * OUTPUT_FORMAT comment) and every region name is in one shared namespace
 * by the time the whole script is assembled, so this block's position,
 * AFTER mmc3-helper.ld's own, is what actually controls final file order.
 *
 * Order is the whole layout contract:
 * - The banked domains come FIRST, and THIS ORDER IS WHAT ASSIGNS THEIR BANK
 *   NUMBERS: 001 -> bank 0, 002 -> bank 1, 003 -> bank 2, 004 -> bank 3,
 *   since a bank is just file offset / 0x2000. Their MEMORY ORIGINs above
 *   and their section()s in demo/src/banks.hpp both hard-code those same
 *   numbers; reordering these four lines silently re-banks every domain and
 *   nothing will warn you.
 * - prg_rom_spare is the zero padding that makes the file exactly
 *   __prg_rom_size. It is EMPTY in this build -- __mmc3_reserved_banks = 4
 *   hands all four leftover banks to the domains above -- and it is the only
 *   thing that grows if __prg_rom_size does without new domains to match.
 * - prg_rom_switchable then prg_rom_fixed occupy the top banks, in that
 *   order: real MMC3 hardware always shows the true last bank at $E000, and
 *   the second-to-last at $C000 -- which is prg_rom_switchable's own third
 *   window, by construction, at any ROM size. */
OUTPUT_FORMAT {
  FULL(prg_rom_001)
  FULL(prg_rom_002)
  FULL(prg_rom_003)
  FULL(prg_rom_004)
  FULL(prg_rom_spare)
  FULL(prg_rom_switchable)
  FULL(prg_rom_fixed)
  FULL(chr_rom)
}
