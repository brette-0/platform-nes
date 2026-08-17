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
 * choice, which is exactly why it doesn't live in mmc3-helper.ld. 128 KiB
 * (16 banks), doubled from the prior 64 KiB to give prg_rom_spare room for
 * upcoming domains (e.g. a level-data bank) without displacing anything
 * already claimed.
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
PROVIDE(__prg_rom_size = 128);
PROVIDE(__chr_rom_size = 8);

/* Seven banked domains, claiming banks the resident image doesn't use: level
 * code (0), level data (1), audio (2), audio_data (3), cold (4), actors (5),
 * and level's dynamic-plane source (6) -- each pulled out of what would
 * otherwise be prg_rom_spare (see banks.hpp's ::cold_tag / ::actor_tag /
 * ::level_dynamic_tag). MUST be set before `INCLUDE mmc3-helper.ld`: it is
 * what shortens prg_rom_spare, and PROVIDE() only takes its
 * first-encountered value. */
PROVIDE(__mmc3_reserved_banks = 7);

/* ONE resident window ($C000-$DFFF, the hardwired second-to-last bank), not
 * the default three. Both switchable windows are handed to the level domain
 * below, which needs 16 KiB contiguous -- level's code alone measures 13,371
 * bytes, and no farcall can enter a domain wider than one window anyway.
 *
 * The consequence is the whole point and the whole risk: while level's banks
 * are mapped, NOTHING at $8000-$BFFF exists except level. Every other symbol
 * in this ROM must therefore fit in the 8 KiB at $C000-$DFFF or in the fixed
 * bank. It does, comfortably (~2.9 KiB measured), but nothing enforces it --
 * a symbol that lands in the level domain's address range without being
 * level's is simply gone whenever level is running. */
PROVIDE(__mmc3_resident_windows = 1);

/* Level's code (main(), ColMapSeed/Track/Stamp/SlideLeft/Right, and
 * everything else level::-namespaced) has NO special placement rule any
 * more, and therefore needs none of the "declare before INCLUDE" ordering
 * the old combined level domain required: that requirement existed only
 * because a plain namespace-mangled wildcard (`*(.text._ZN5level*)`)
 * competes with nes.ld's own generic `*(.text .text.*)` catch-all for the
 * SAME kind of section name, and ld resolves that race by declaration order.
 * ::LEVEL_CODE/::LEVEL_DATA/::ACTORS/::COLD/::FIXED all route through
 * CREATE_SEGMENT_KEYWORD instead, which gives each piece of code or data its
 * own uniquely-named section (`.prg_rom_level_code` etc.) that no generic
 * wildcard can ever match -- so those rules live in the ordinary post-INCLUDE
 * block below, same as audio/cold/actors. Level's own code isn't untagged any
 * more (::LEVEL_CODE covers it), but anything that stays untagged still falls
 * through to the resident catch-all exactly as before. */

INCLUDE mmc3-helper.ld

/* ------------------------------------------------------------------------
 * BANKED DOMAINS -- banks 0-5, 8 KiB each. Most reach through window 1
 * ($8000-$9FFF, R6); two (audio data, level's dynamic plane) are pinned to
 * window 2 ($A000-$BFFF, R7) because each has to be mappable AT THE SAME
 * TIME as a specific window-1 partner -- see their own comments below.
 *
 * ORIGIN is ((bank + 1) * 0x10000) + (0x8000 or 0xA000), and the +1 is
 * load-bearing TWICE over -- see mmc3.hpp's section_t ENCODING comment for
 * the full account, but in short: (a) the C++ side treats a zero high half
 * as "no bank encoded", so an unbiased bank 0 would be indistinguishable
 * from unset, and (b) an unbiased bank 0 region would sit at ORIGIN 0x8000,
 * overlapping this project's own .text, which ld.lld rejects outright
 * ("section .text virtual address range overlaps with .prg_rom_001" --
 * confirmed against a real link, not assumed). The high bits never reach the
 * emitted 6502 code: a JSR operand has 16 bits, so every one of these
 * truncates back to a plain $8000/$A000 call.
 *
 * A domain's REAL bank number is its file offset / 0x2000, which is decided
 * entirely by the FULL() order in the OUTPUT_FORMAT block at the bottom of
 * this file -- NOT by these ORIGINs, and NOT by the numbers in their names.
 * The two are kept in agreement by hand here (level_code is FULL()'d first,
 * so it is bank 0, so its ORIGIN encodes 0+1), exactly the arithmetic that
 * shipped a real crash in the VRC1 rollout when it was got wrong -- see
 * BANKED_CALL_THEORY.txt's "CRITICAL POST-PHASE-4 FIX". Change one, change
 * all three: the FULL() order, the ORIGIN here, and the section() in
 * demo/src/banks.hpp.
 * ------------------------------------------------------------------------ */
MEMORY {
  /* THE LEVEL CODE BANK: bank 0, window 1 ($8000, R6). Level's own code --
   * ::LEVEL_CODE-tagged in levels.cpp/collision_map.cpp -- reads the static
   * plane and the dynamic plane's ROM source directly out of window 2, so it
   * stays mapped for the whole level session rather than being farcalled per
   * read. See banks.hpp's ::level_code_tag. */
  prg_rom_level_code : ORIGIN = 0x00018000, LENGTH = 0x2000  /* bank 0, window 1 */
  /* THE LEVEL DATA BANK: bank 1, window 2 ($A000, R7). The static plane
   * (TileData_1_1) and the dynamic plane's ROM source (DynData_1_1,
   * DynLengths_1_1) -- ::LEVEL_DATA-tagged -- must be mappable at the SAME
   * TIME as prg_rom_level_code, since that code reads both directly; that's
   * the window-2 placement, same simultaneous-mapping constraint as audio's
   * code/data split below. Both are entered together, once, by banks.hpp's
   * ::EnterLevelBanks and held for the whole level -- see ::level_data_tag. */
  prg_rom_level_data : ORIGIN = 0x0002a000, LENGTH = 0x2000  /* bank 1, window 2 */
  /* THE AUDIO CODE BANK: FamiStudio's engine and platform-nes's audio module,
   * and nothing else. Their adjacency is a hard contract -- src/nes/audio.cpp
   * does no bank switching, it just calls the engine, which is only valid
   * because they share a bank. PLATFORM_NES_AUDIO_SECTION (local.cmake) and
   * FAMISTUDIO_CA65_CODE_SEGMENT (demo/famistudio_config.s) are the two knobs
   * that put them here together. */
  prg_rom_audio : ORIGIN = 0x00038000, LENGTH = 0x2000  /* bank 2, window 1 */
  /* AUDIO DATA -- songs and SFX. A SEPARATE BANK on purpose: the data has no
   * reason to sit beside the engine, and forcing it there would cap every song
   * this cart can hold at whatever is left of bank 2. Music and SFX arrive in
   * their own sections (.prg_rom_music/.prg_rom_sfx) and are merged here only
   * because THIS project chose to; splitting them across banks later is a
   * change to this block alone.
   *
   * WINDOW 2 ($A000) is the one placement rule that is not free choice: the
   * engine walks this data while it executes, so both must be mapped at once,
   * which means they cannot share a window. mmc3::CallPairedBlock asserts it. */
  prg_rom_audio_data : ORIGIN = 0x0004a000, LENGTH = 0x2000  /* bank 3, window 2 */
  /* bank 2, but encoded for WINDOW 2 ($A000, R7) rather than window 1 --
   * note the A000 in the ORIGIN. This is the audio engine's DATA bank, and it
   * has to be mappable at the same time as the engine's CODE bank (002, R6):
   * the engine dereferences song pointers while it runs, and MMC3 has exactly
   * two switchable windows, so a same-window pair would have the data
   * immediately displace the code. mmc3::CallPairedBlock static_asserts that
   * the two tags differ here. */
  /* THE COLD BANK: bank 4, window 1 ($8000, R6). Home for level code that
   * only ever runs once (level's own one-time setup, currently) -- reached by
   * an ordinary mmc3::CallInBlock<cold_tag>, not a permanent switch like the
   * level-data domains above, since nothing here needs to stay mapped once
   * the call returns. See banks.hpp's ::cold_tag. */
  prg_rom_cold : ORIGIN = 0x00058000, LENGTH = 0x2000  /* bank 4, window 1 */
  /* THE ACTORS BANK: bank 5, window 1 ($8000, R6). Actor's own methods
   * (Move/Start/Update) and Player's (Update/Reset) -- ::ACTORS-tagged in
   * actor.cpp/player.cpp. Unlike prg_rom_cold this is called every frame, not
   * once: level::main() reaches it through mmc3::Call<level::UpdateActors>,
   * so the whole roster updates behind one window switch instead of paying
   * one per actor. See banks.hpp's ::actor_tag for the size/placement
   * rationale. */
  prg_rom_actors : ORIGIN = 0x00068000, LENGTH = 0x2000  /* bank 5, window 1 */
  /* THE LEVEL DYNAMIC-SOURCE BANK: bank 6, window 2 ($A000, R7). DynData_1_1
   * -- ::LEVEL_DYNAMIC-tagged -- read ONCE at load, through an explicit
   * mmc3::CallInBlock<level_dynamic_tag> (LoadLevel, levels.cpp), NOT
   * permanently pinned like prg_rom_level_data. See banks.hpp's
   * ::level_dynamic_tag. */
  prg_rom_level_dynamic : ORIGIN = 0x0007a000, LENGTH = 0x2000  /* bank 6, window 2 */
}

/* Published for the build-time space report -- see mmc3-helper.ld's own
 * __space_* block for why an empty bank needs these to be visible at all. */
__space_origin_prg_rom_level_code = ORIGIN(prg_rom_level_code);
__space_length_prg_rom_level_code = LENGTH(prg_rom_level_code);
__space_origin_prg_rom_level_data = ORIGIN(prg_rom_level_data);
__space_length_prg_rom_level_data = LENGTH(prg_rom_level_data);
__space_origin_prg_rom_audio = ORIGIN(prg_rom_audio);
__space_length_prg_rom_audio = LENGTH(prg_rom_audio);
__space_origin_prg_rom_audio_data = ORIGIN(prg_rom_audio_data);
__space_length_prg_rom_audio_data = LENGTH(prg_rom_audio_data);
__space_origin_prg_rom_cold = ORIGIN(prg_rom_cold);
__space_length_prg_rom_cold = LENGTH(prg_rom_cold);
__space_origin_prg_rom_actors = ORIGIN(prg_rom_actors);
__space_length_prg_rom_actors = LENGTH(prg_rom_actors);
__space_origin_prg_rom_level_dynamic = ORIGIN(prg_rom_level_dynamic);
__space_length_prg_rom_level_dynamic = LENGTH(prg_rom_level_dynamic);

SECTIONS {
  /* Level's interrupt path needs no rule at all: nmi_handler, irq_handler and
   * ApplyHudSplit are tagged ::FIXED in level.cpp, so they land in
   * .prg_rom_fixed and mmc3-helper.ld's existing rule takes them. */
  .prg_rom_level_code : { *(.prg_rom_level_code .prg_rom_level_code.*) } >prg_rom_level_code
  .prg_rom_level_data : { *(.prg_rom_level_data .prg_rom_level_data.*) } >prg_rom_level_data
  .prg_rom_audio : { *(.prg_rom_audio .prg_rom_audio.*) } >prg_rom_audio
  .prg_rom_audio_data : {
    *(.prg_rom_music .prg_rom_music.*)
    *(.prg_rom_sfx .prg_rom_sfx.*)
  } >prg_rom_audio_data
  .prg_rom_cold : { *(.prg_rom_cold .prg_rom_cold.*) } >prg_rom_cold
  .prg_rom_actors : { *(.prg_rom_actors .prg_rom_actors.*) } >prg_rom_actors
  .prg_rom_level_dynamic : { *(.prg_rom_level_dynamic .prg_rom_level_dynamic.*) } >prg_rom_level_dynamic
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
PROVIDE(__audio_engine_size = SIZEOF(.prg_rom_audio));
ASSERT(__audio_engine_size > 0 || !DEFINED(nmi_handler),
       "The audio bank is empty -- demo/famistudio_config.s's FAMISTUDIO_CA65_CODE_SEGMENT no longer decodes to .prg_rom_audio (ld.lld XO65 escapes: _p is '.', __ is '_'), so audio::Update() would call an engine that is not in the ROM.")

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
 *   NUMBERS: level_code -> bank 0, level_data -> bank 1, audio -> bank 2,
 *   audio_data -> bank 3, cold -> bank 4, actors -> bank 5,
 *   level_dynamic -> bank 6, since a bank is just file offset / 0x2000.
 *   Their MEMORY ORIGINs above and their section()s in demo/src/banks.hpp
 *   both hard-code those same numbers; reordering these lines silently
 *   re-banks every domain and nothing will warn you.
 * - prg_rom_spare is the zero padding that makes the file exactly
 *   __prg_rom_size. __mmc3_reserved_banks = 7 hands all seven leftover banks
 *   to the domains above, leaving prg_rom_spare whatever remains, and it is
 *   the only thing that grows if __prg_rom_size does without new domains to
 *   match.
 * - prg_rom_switchable then prg_rom_fixed occupy the top banks, in that
 *   order: real MMC3 hardware always shows the true last bank at $E000, and
 *   the second-to-last at $C000 -- which is prg_rom_switchable's own third
 *   window, by construction, at any ROM size. */
OUTPUT_FORMAT {
  FULL(prg_rom_level_code)
  FULL(prg_rom_level_data)
  FULL(prg_rom_audio)
  FULL(prg_rom_audio_data)
  FULL(prg_rom_cold)
  FULL(prg_rom_actors)
  FULL(prg_rom_level_dynamic)
  FULL(prg_rom_spare)
  FULL(prg_rom_switchable)
  FULL(prg_rom_fixed)
  FULL(chr_rom)
}
