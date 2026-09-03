/*
 * Demo's own top-level linker script -- CONSUMER-OWNED, not library code.
 *
 * Converted from VRC1 to MMC3: INCLUDEs mmc3-helper.ld instead of
 * vrc1-helper.ld. The old VRC1-only smoke-test domains are gone (vrc1.cpp
 * no longer links).
 *
 * FamiStudio has no dedicated bank any more: this demo has no frequent
 * cross-bank calling, so its compiled code just lives as ordinary
 * always-resident content in prg_rom_switchable -- audio.cpp calls its
 * entry points directly, no farcall wrapper needed.
 */

/* This demo's own ROM size -- a project choice, not an MMC3 fact. 128 KiB
 * (16 banks), doubled from 64 KiB for headroom for new banked domains.
 * Any power of 2 from 32 up to 512 KiB (MMC3's hardware max) works with no
 * other edit -- mmc3-helper.ld anchors the resident image to the TOP of the
 * file and pads the rest into prg_rom_spare.
 *
 * TRACK THIS BY HAND: demo/src/header.hpp's HEADER() prg_rom_bytes argument
 * must match (the NES2.0 header can't see linker symbols). Growing this does
 * NOT grow the flat resident image ($8000-$DFFF) -- new banks are only
 * reachable through MMC3_BANKED() domains. */
PROVIDE(__prg_rom_size = 128);
PROVIDE(__chr_rom_size = 8);

/* Eight banked domains, claiming banks that would otherwise be prg_rom_spare:
 * level code (0), level data (1), audio (2), audio_data (3), cold (4),
 * actors (5), level's dynamic-plane source (6), level graphics (7) -- see
 * banks.hpp. MUST be set before `INCLUDE mmc3-helper.ld`: PROVIDE() only
 * takes its first value. */
PROVIDE(__mmc3_reserved_banks = 8);

/* ONE resident window ($C000-$DFFF), not the default three. Both switchable
 * windows go to the level domain below (16 KiB contiguous, since no farcall
 * can enter a domain wider than one window).
 *
 * The risk: while level's banks are mapped, NOTHING at $8000-$BFFF exists
 * except level. Everything else must fit in $C000-$DFFF or the fixed bank --
 * it does (~2.9 KiB measured), but nothing enforces it. */
PROVIDE(__mmc3_resident_windows = 1);

/* Level's code has no special placement rule any more, so it needs none of
 * the old "declare before INCLUDE" wildcard-ordering trick. ::LEVEL_CODE/
 * ::LEVEL_DATA/::ACTORS/::COLD/::FIXED all route through
 * CREATE_SEGMENT_KEYWORD, giving each piece its own uniquely-named section
 * that no generic wildcard can match -- so those rules live in the ordinary
 * post-INCLUDE block below. Anything untagged still falls through to the
 * resident catch-all as before. */

INCLUDE mmc3-helper.ld

/* ------------------------------------------------------------------------
 * BANKED DOMAINS -- banks 0-6, 8 KiB each. Most reach through window 1
 * ($8000-$9FFF, R6); audio_data and level_dynamic are pinned to window 2
 * ($A000-$BFFF, R7) because each must be mappable at the same time as a
 * specific window-1 partner -- see their own comments below.
 *
 * ORIGIN is ((bank + 1) * 0x10000) + (0x8000 or 0xA000) -- see mmc3.hpp's
 * section_t ENCODING comment for why the +1 bias exists. The high bits never
 * reach emitted code (a JSR operand is 16 bits), so every one of these
 * truncates back to a plain $8000/$A000 call.
 *
 * A domain's REAL bank number is its file offset / 0x2000, decided by the
 * FULL() order in OUTPUT_FORMAT at the bottom of this file -- NOT by these
 * ORIGINs or the numbers in their names. The two are kept in agreement by
 * hand (level_code is FULL()'d first, so it's bank 0, so its ORIGIN encodes
 * 0+1). Change one, change all three: FULL() order, ORIGIN here, and
 * section() in demo/src/banks.hpp.
 * ------------------------------------------------------------------------ */
MEMORY {
  /* LEVEL CODE: bank 0, window 1. Level's own code -- reads the static plane
   * and dynamic-plane ROM source directly out of window 2, so it stays
   * mapped for the whole level session. See banks.hpp's ::level_code_tag. */
  prg_rom_level_code : ORIGIN = 0x00018000, LENGTH = 0x2000  /* bank 0, window 1 */
  /* LEVEL DATA: bank 1, window 2. TileData_1_1 + DynLengths_1_1. Not paired
   * with prg_rom_level_code by a compile-time tag -- level::LoadLevel switches
   * window 2 here with a runtime bank number (banks.hpp's ::LevelDataBank)
   * and holds it for the whole level session. */
  prg_rom_level_data : ORIGIN = 0x0002a000, LENGTH = 0x2000  /* bank 1, window 2 */
  /* AUDIO CODE: FamiStudio's engine + platform-nes's audio module. Adjacency
   * is a hard contract -- audio.cpp calls the engine with no bank switching,
   * valid only because they share a bank. */
  prg_rom_audio : ORIGIN = 0x00038000, LENGTH = 0x2000  /* bank 2, window 1 */
  /* AUDIO DATA: songs and SFX, separate so they aren't capped by bank 2's
   * leftover space. WINDOW 2 is forced: the engine walks this data while
   * executing, so both must be mapped at once (mmc3::CallPairedBlock
   * asserts it). */
  prg_rom_audio_data : ORIGIN = 0x0004a000, LENGTH = 0x2000  /* bank 3, window 2 */
  /* COLD: bank 4, window 1. Code that runs once (level's one-time setup),
   * reached via mmc3::CallInBlock<cold_tag>. See ::cold_tag. */
  prg_rom_cold : ORIGIN = 0x00058000, LENGTH = 0x2000  /* bank 4, window 1 */
  /* ACTORS: bank 5, window 1. Actor's methods + Player's, reached every
   * frame via mmc3::Call<level::UpdateActors> so the whole roster updates
   * behind one switch. See ::actor_tag. */
  prg_rom_actors : ORIGIN = 0x00068000, LENGTH = 0x2000  /* bank 5, window 1 */
  /* LEVEL DYNAMIC-SOURCE: bank 6, window 2. DynData_1_1, read once at load via
   * a raw mmc3::SwitchBank(window2Control, LevelDynamicBank(n)) in LoadLevel,
   * not permanently pinned like prg_rom_level_data. See ::LevelDynamicBank. */
  prg_rom_level_dynamic : ORIGIN = 0x0007a000, LENGTH = 0x2000  /* bank 6, window 2 */
  /* LEVEL GRAPHICS: bank 7, window 2. Metatile planes, level palette data,
   * and the level's string/charmap data -- reached via
   * mmc3::CallInBlock<level_graphics_tag>, switched in and back out around
   * the reads that need it. See banks.hpp's ::level_graphics_tag. */
  prg_rom_level_graphics : ORIGIN = 0x0008a000, LENGTH = 0x2000  /* bank 7, window 2 */
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
__space_origin_prg_rom_level_graphics = ORIGIN(prg_rom_level_graphics);
__space_length_prg_rom_level_graphics = LENGTH(prg_rom_level_graphics);

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
  .prg_rom_level_graphics : { *(.prg_rom_level_graphics .prg_rom_level_graphics.*) } >prg_rom_level_graphics
}

/* Moves the C writable arena (.data/.bss/heap/soft stack) into PRG-RAM
 * instead of system RAM, freeing the console's 1.5 KiB onboard RAM for OAM
 * buffers and hot resident state.
 *
 * MUST come AFTER `INCLUDE mmc3-helper.ld`: prg_ram is declared in that
 * file's MEMORY block.
 *
 * __stack sits on the top BYTE of prg_ram (not one past the end), same
 * convention as the SDK's nes/lib/c-in-prg-ram-fixed.ld. */
REGION_ALIAS("c_writeable", prg_ram)
__stack = ORIGIN(prg_ram) + LENGTH(prg_ram) - 1;

/* Heap: a hard-reserved 128 bytes, carved out of the TOP of prg_ram and
 * walled off from both .data/.bss and the softstack -- not the SDK default
 * of "however many bytes happen to be free between wherever .bss/.noinit
 * end and wherever the stack has grown to".
 *
 * noinit.ld (pulled in via INCLUDE mmc3-helper.ld above) already set
 * __heap_start to that default (right after .data/.bss/.noinit, growing up
 * toward __stack) -- __static_writeable_end captures that SDK-computed
 * value BEFORE the override below discards it, so the ASSERT still catches
 * .data/.bss/.noinit (including any future __attribute__((section(".noinit")))
 * globals) growing far enough to collide with the reserved heap.
 *
 * __heap_start is moved to the last 128 bytes of prg_ram, __heap_default_limit
 * is capped at exactly that (a weak SDK default of 4096 would otherwise let
 * the allocator walk past the reservation with no error), and __stack is
 * pulled down to sit directly below it. Both boundaries are now fixed
 * addresses baked into the layout, not "however big something else happened
 * to be" -- .data/.bss can't grow into the heap (ASSERT below catches it at
 * link time) and the stack can't grow into it either (it starts one byte
 * below __heap_start, same top-of-region convention as __stack itself). */
__static_writeable_end = __heap_start;
__heap_default_limit = 128;
__heap_start = ORIGIN(prg_ram) + LENGTH(prg_ram) - 128;
__stack = __heap_start - 1;
ASSERT(__static_writeable_end <= __heap_start,
       "prg_ram's .data/.bss/.noinit grew into the reserved 128-byte heap at the top of prg_ram -- shrink static writeable usage or grow prg_ram/__prg_ram_size.")

/* Regression guard: the audio engine's code really did land in its bank. A
 * typo in FAMISTUDIO_CA65_CODE_SEGMENT (demo/famistudio_config.s) is
 * otherwise silent -- the segment lands elsewhere and audio.cpp's farcalls
 * bank in an empty 8 KiB.
 *
 * Zero IS tolerated via DEFINED(nmi_handler): CMake's compiler-ID/ABI probes
 * link a bare `int main(){}` with no audio in sight. */
PROVIDE(__audio_engine_size = SIZEOF(.prg_rom_audio));
ASSERT(__audio_engine_size > 0 || !DEFINED(nmi_handler),
       "The audio bank is empty -- demo/famistudio_config.s's FAMISTUDIO_CA65_CODE_SEGMENT no longer decodes to .prg_rom_audio (ld.lld XO65 escapes: _p is '.', __ is '_'), so audio::Update() would call an engine that is not in the ROM.")

/* mmc3-helper.ld FULL()s only nes2_header; every PRG region is this
 * project's job, in this exact order -- OUTPUT_FORMAT is additive across
 * INCLUDEd files, and this block's position (AFTER mmc3-helper.ld's own)
 * controls final file order.
 *
 * Order is the whole layout contract:
 * - Banked domains come FIRST, and THIS ORDER ASSIGNS THEIR BANK NUMBERS
 *   (level_code=0, level_data=1, audio=2, audio_data=3, cold=4, actors=5,
 *   level_dynamic=6, level_graphics=7 -- bank is just file offset / 0x2000).
 *   Their MEMORY ORIGINs above and section()s in banks.hpp hard-code those
 *   same numbers; reordering these lines silently re-banks everything with
 *   no warning.
 * - prg_rom_spare is the zero padding filling out __prg_rom_size.
 * - prg_rom_switchable then prg_rom_fixed occupy the top banks: real MMC3
 *   hardware always shows the last bank at $E000, second-to-last at $C000. */
OUTPUT_FORMAT {
  FULL(prg_rom_level_code)
  FULL(prg_rom_level_data)
  FULL(prg_rom_audio)
  FULL(prg_rom_audio_data)
  FULL(prg_rom_cold)
  FULL(prg_rom_actors)
  FULL(prg_rom_level_dynamic)
  FULL(prg_rom_level_graphics)
  FULL(prg_rom_spare)
  FULL(prg_rom_switchable)
  FULL(prg_rom_fixed)
  FULL(chr_rom)
}
