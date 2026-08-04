/*
 * Demo's own top-level linker script -- CONSUMER-OWNED, not library code.
 *
 * BANKED CALL, Phase 5: this is the "thin, user-owned top-level script"
 * BANKED_CALL_THEORY.txt's "THE FINAL LINKER SCRIPT MUST BE USER-MADE"
 * section describes -- INCLUDEs the library's own vrc1-helper.ld for the
 * genuinely hardware/mechanical facts, and owns everything that's actually
 * this project's own decision: ROM size, and every BANKED()-tagged domain
 * this demo happens to use (including this rollout's own Phase 2-4 smoke
 * tests, which are exactly as much "this project's own code" as anything
 * else here -- the library never knows they exist).
 */

/* Phase 2 smoke test reserves 64 bytes off prg_rom_switchable's own tail
 * for its dedicated carve-out (prg_rom_window_test below) -- see
 * vrc1-helper.ld's own comment on __prg_rom_switchable_reserved for why
 * this hook exists instead of vrc1-helper.ld hardcoding the shrink itself.
 *
 * MUST come BEFORE `INCLUDE vrc1-helper.ld` below, not after -- confirmed
 * the hard way (a real, silent MEMORY region overlap, not a build error):
 * PROVIDE() only takes effect on the FIRST definition it encounters in
 * parse order, and vrc1-helper.ld provides its own default (0) for this
 * same symbol. Declaring it here after the INCLUDE compiles and links
 * without complaint -- the helper's PROVIDE(=0) already "wins" by then,
 * silently leaving prg_rom_switchable unshrunk and overlapping
 * prg_rom_window_test's own declared address range. */
PROVIDE(__prg_rom_switchable_reserved = 0x40);

INCLUDE vrc1-helper.ld

/* This demo's own ROM size -- NOT a VRC1 fact, purely this project's
 * choice, which is exactly why it doesn't live in vrc1-helper.ld. 64 KiB:
 * the previous 56 KiB (Phase 3's explicit alternate bank + Phase 4's two-
 * window oversized domain) plus ONE more bank for Phase 6's FamiStudio
 * domain (prg_rom_famistudio below) -- see BANKED_CALL_THEORY.txt for the
 * full accounting of which banks are used for what. 64 KiB specifically
 * (not e.g. 72) because it's the next NES2.0-representable size: the
 * header's exponent-multiplier form needs bytes = 2^E * (odd, <=7), and
 * 72 KiB (2^13*9) fails that (9 is not representable) while 64 KiB
 * (2^16*1) trivially is -- checked against nes2.hpp's own
 * rom_size_representable() before picking this, not by trial and error. */
PROVIDE(__prg_rom_size = 64);
PROVIDE(__chr_rom_size = 8);

ASSERT(__prg_rom_size == 64,
       "This demo currently only supports its existing 64 KiB PRG-ROM (8 banks: 3 switchable defaults + 1 explicit alternate for window3Control + 2 for the oversized window2+window3 domain + 1 for FamiStudio + 1 fixed); growing further needs another explicit .prg_rom_bankN region below.")

/* Build-time-only sanity check that __prg_rom_switchable_reserved's PROVIDE
 * above actually took effect, rather than being silently shadowed by
 * vrc1-helper.ld's own default -- see this file's own comment on that
 * PROVIDE for the real bug this would have caught immediately. Checks the
 * LIVE symbol value, not a hardcoded arithmetic restatement of what it
 * should be (the ASSERT below this one made exactly that mistake once). */
ASSERT(__prg_rom_switchable_reserved == 0x40,
       "__prg_rom_switchable_reserved is not 0x40 -- either its PROVIDE() above got moved after `INCLUDE vrc1-helper.ld` (must stay before it, see that PROVIDE's own comment) or prg_rom_window_test's own reservation size changed without updating this ASSERT to match.")

/* Build-time-only sanity check: prg_rom_oversized's ORIGIN below encodes
 * base bank 4 (matching bank_layout<oversized_tag>::section() in
 * vrc1.cpp). This ASSERT exists specifically because a WRONG hand-entered
 * base bank (5, not 4) shipped without one and produced a real runtime
 * crash -- see BANKED_CALL_THEORY.txt's Phase 4 notes. Checked here, right
 * after PRG-ROM's own total-size ASSERT, rather than after the MEMORY
 * block below: this is a property of how the earlier regions' LENGTHs sum,
 * independent of prg_rom_oversized's own declaration, so if this fires,
 * the fix is almost always in vrc1.cpp's hand-entered literal, not in the
 * MEMORY block itself. Checks the LIVE reserved value too, not just a
 * restated literal -- this ASSERT alone previously stayed green (it's a
 * hardcoded arithmetic tautology, 0x8000==0x8000, true regardless of
 * whether the reservation actually applied) while the real MEMORY-overlap
 * bug above went undetected; kept as a second, independent check now that
 * the real one exists, not because this one was ever sufficient alone. */
ASSERT((0x6000 - __prg_rom_switchable_reserved) + 0x40 + 0x2000 == 4 * 0x2000,
       "prg_rom_oversized's real bank (computed from where the regions before it in OUTPUT_FORMAT's ordering actually end) no longer starts at bank 4 -- update the hand-entered base bank in bank_layout<oversized_tag>::section() (vrc1.cpp) AND prg_rom_oversized's ORIGIN below to match, or this silently repeats the exact bug this ASSERT was added to catch.")

/* Same category of check, for Phase 6's prg_rom_famistudio below: banks
 * 0-3 (0x8000 worth) plus oversized's two banks (0x4000) end exactly at
 * bank 6's start. Written this way (not re-deriving it as "4*0x2000+0x4000")
 * specifically so it stays a genuine cross-check against the OTHER ASSERT's
 * own arithmetic, not just a restatement of the same expression. */
ASSERT((0x6000 - __prg_rom_switchable_reserved) + 0x40 + 0x2000 + 0x4000 == 6 * 0x2000,
       "prg_rom_famistudio's real bank (computed from where the regions before it in OUTPUT_FORMAT's ordering actually end) no longer starts at bank 6 -- update the hand-entered base bank in bank_layout<famistudio_tag>::section() (audio.cpp) AND prg_rom_famistudio's ORIGIN below to match, or this repeats the exact class of bug BANKED_CALL_THEORY.txt's Phase 4 notes describe.")

MEMORY {
  /* BANKED CALL, Phase 2 smoke test ONLY (see bank_layout<window_test_tag>,
   * vrc1.cpp) -- a small, dedicated carve-out at a FIXED, hand-known address
   * (window index 2, window3Control's range), reserved exclusively for
   * .banked_call_window_test. Earlier revision let this section share
   * prg_rom_switchable and land wherever the linker's default layout put it,
   * resolved via a genuinely link-time-computed rom_address (LOADADDR(),
   * read back into C++ at runtime). Reverted: that runtime resolution
   * costs ~120 bytes of real 6502 code PER Call<Fn> call site (measured --
   * a 32-bit subtract/shift-divide/branch the optimizer never manages to
   * fold away, despite the address itself being link-time-fixed), because
   * the compiler can't see through bank_layout<Tag>::section()'s runtime
   * symbol read far enough to prove the division's result is constant. A
   * dedicated region with a HAND-KNOWN origin sidesteps this category of
   * cost entirely: bank_layout<window_test_tag>::section() is a real,
   * standard-C++ constexpr literal (0xdfc0, matching this region's ORIGIN),
   * so Call<Fn> resolves it exactly like a hand-supplied ::Long window
   * argument would -- zero runtime resolution cost, confirmed by
   * disassembly. This is also the pattern any REAL tagged domain (a level
   * chunk, an audio module) should reach for by default: give it its own
   * MEMORY region with a known ORIGIN. The shared/computed-address approach
   * this replaces remains available in bank_layout<Tag>'s design for a
   * domain that genuinely can't be given a fixed address (see
   * BANKED_CALL_THEORY.txt) -- it's just no longer the default/only shape,
   * now that its real cost is measured. */
  prg_rom_window_test : ORIGIN = 0x8000 + 0x6000 - 0x40, LENGTH = 0x40
  /* BANKED CALL, Phase 3: bank 3, a second real physical bank for
   * window3Control's slot ($C000-$DFFF), alongside the default bank
   * ordinary content there already uses (see vrc1-helper.ld's file comment
   * for the ORIGIN = bank*0x10000 + real_address encoding this and every
   * explicit alternate bank must follow). Exclusively for content tagged
   * BANKED(".prg_rom_bank3", ...) -- never reached except through Call<Fn>,
   * since nothing boots into it by default (::_reset still only
   * initializes window3Control to bank 2, unchanged). */
  prg_rom_bank3       : ORIGIN = (3 * 0x10000) + 0xc000, LENGTH = 0x2000
  /* BANKED CALL, Phase 4: the oversized domain -- see BANKED_CALL_THEORY.txt
   * for why this is ONE 16 KiB region (not two), and why that's safe.
   * Reserved exclusively for content tagged BANKED(".prg_rom_oversized",
   * oversized, ...) in vrc1.cpp; ordinary automatic linker placement fills
   * it just like prg_rom_switchable does for untagged code. */
  prg_rom_oversized   : ORIGIN = (4 * 0x10000) + 0xa000, LENGTH = 0x4000
  /* BANKED CALL, Phase 6: FamiStudio's own domain -- the real opaque-vendor-
   * library case (BANKED_CALL_THEORY.txt's "EXTERNALLY-BUILT LIBRARIES"
   * section). Bank 6, window3Control's slot again (same window as bank3
   * and window_test's default bank -- arbitrary choice, any window would
   * do; VRC1 lets any bank number map into any window via its own 4-bit
   * select register per window). Sized for ONE window (8 KiB) as a first
   * measurement, not a guess locked in permanently -- CallInSection already
   * transparently upgrades to the two-window CallInWindows2 path if
   * __famistudio_domain_size (audio.cpp) ever measures larger than this,
   * so growing this LENGTH later (if FamiStudio's real footprint needs it)
   * doesn't need any C++-side rework, only this one line and the matching
   * total ROM size above. */
  prg_rom_famistudio  : ORIGIN = (6 * 0x10000) + 0xc000, LENGTH = 0x2000
}

SECTIONS {
  /* BANKED CALL, Phase 2 smoke test -- routed into its own dedicated,
   * fixed-address region (see prg_rom_window_test's own comment above for
   * why). */
  .banked_call_window_test : { *(.banked_call_window_test) } >prg_rom_window_test
  /* BANKED CALL, Phase 3 smoke test -- bank3's own dedicated region (see
   * prg_rom_bank3's own comment above). */
  .prg_rom_bank3 : { *(.prg_rom_bank3 .prg_rom_bank3.*) } >prg_rom_bank3
  /* BANKED CALL, Phase 4 smoke test -- the oversized domain's own dedicated
   * region (see prg_rom_oversized's own comment above). */
  .prg_rom_oversized : { *(.prg_rom_oversized .prg_rom_oversized.*) } >prg_rom_oversized
  /* BANKED CALL, Phase 6 -- FamiStudio's own domain (see prg_rom_famistudio's
   * own comment above). FSCODE (no leading dot -- ca65/xo65 segment names
   * are bare, unlike ELF section naming convention; confirmed empirically,
   * not assumed, via a minimal real xo65+ELF link before this was wired in
   * here) MUST match CMakeLists.txt's FAMISTUDIO_SEGMENT cache variable's
   * value literally -- this file isn't CMake-templated, so the two can't
   * share one source of truth the way famistudio_config.s and audio.cpp's
   * own -D do. The ASSERT below this SECTIONS block is the safety net for
   * the two drifting apart. Deliberately underscore-free: ld.lld's XO65
   * reader (the ca65/xo65 object-file linking path) runs every segment name
   * through its OWN underscore-based escape scheme when decoding it into an
   * ELF-shaped section -- a segment named "FAMISTUDIO_CODE" was rejected
   * outright ("unknown underscore escape"), confirmed with a real build.
   * That's a separate, lower layer than the "no leading dot" fact above. */
  FSCODE : { *(FSCODE) } >prg_rom_famistudio
}

/* Build-time-only sanity check: bank_layout<window_test_tag>::section() in
 * vrc1.cpp hand-enters 0xdfc0 as a constexpr literal (matching
 * prg_rom_window_test's ORIGIN above), specifically so Call<Fn> never has
 * to read this back at runtime -- see that region's own comment. This
 * ASSERT costs nothing at runtime (link-time only); it exists purely to
 * catch the two numbers drifting apart, e.g. if prg_rom_window_test's
 * ORIGIN/LENGTH above is ever edited without updating the matching literal
 * in vrc1.cpp. */
PROVIDE(__banked_call_window_test_rom_address = LOADADDR(.banked_call_window_test));
ASSERT(__banked_call_window_test_rom_address == 0xdfc0,
       "bank_layout<window_test_tag>::section() in vrc1.cpp is out of sync with prg_rom_window_test's ORIGIN in link.ld -- update the hand-entered literal to match.")

/* Same build-time-only sanity check, for bank_layout<bank3_test_tag>::section()
 * in vrc1.cpp (Phase 3) -- must equal (3 * 0x10000) + 0xc000, matching
 * prg_rom_bank3's ORIGIN above. */
PROVIDE(__banked_call_bank3_test_rom_address = LOADADDR(.prg_rom_bank3));
ASSERT(__banked_call_bank3_test_rom_address == (3 * 0x10000) + 0xc000,
       "bank_layout<bank3_test_tag>::section() in vrc1.cpp is out of sync with prg_rom_bank3's ORIGIN in link.ld -- update the hand-entered literal to match.")

/* BANKED CALL, Phase 4: __oversized_domain_size is read back into C++ via
 * the extern-symbol-address idiom (bank_layout<oversized_tag>, vrc1.cpp) --
 * genuinely link-computed, unlike every rom_address above, because how
 * much content ends up tagged into this domain isn't something anyone
 * hand-declares (that's the whole point of the feature -- see
 * BANKED_CALL_THEORY.txt's Phase 4 notes). The ASSERT below is a
 * regression guard, not a correctness check the linker would catch
 * otherwise: it exists purely so this domain can never silently shrink
 * back to fitting in one window (e.g. after a refactor removes the padding
 * below) while still claiming to test the two-window case. Zero is
 * explicitly allowed alongside >0x2000, NOT tightened to just >0x2000:
 * CMake's own internal compiler-identification/ABI probes link a bare
 * `int main(){}` with no vrc1.cpp in sight (see vrc1-helper.ld's own
 * PROVIDE(_reset=_start) comment for the same class of bare-probe build),
 * so .prg_rom_oversized is legitimately empty for those, not a regression. */
PROVIDE(__oversized_domain_size = SIZEOF(.prg_rom_oversized));
ASSERT(__oversized_domain_size == 0 || __oversized_domain_size > 0x2000,
       "The Phase 4 oversized-domain smoke test must genuinely exceed one window (8 KiB) -- otherwise it silently stops testing the two-window CallInWindows2 path it exists to prove.")

/* STRONGER than the size check above -- total domain size exceeding one
 * window does NOT by itself guarantee any particular function inside it
 * actually crosses the boundary (confirmed empirically: with less padding
 * than OversizedFuncLow's own body needed, __oversized_domain_size was
 * already >0x2000 while OversizedFuncHigh had STILL landed inside the
 * first window, silently testing nothing extra). This checks the thing
 * that actually matters: OversizedFuncHigh's own address is really past
 * the first window's 0x2000 mark within the domain. OversizedFuncHigh
 * won't exist as a symbol at all in CMake's bare-probe builds (see above),
 * so this is skipped via DEFINED(). */
ASSERT(!DEFINED(_Z17OversizedFuncHighv) ||
       (_Z17OversizedFuncHighv & 0xffff) >= 0xc000,
       "OversizedFuncHigh landed inside window2Control's half of the oversized domain instead of window3Control's -- OversizedFuncLow's padding (vrc1.cpp) needs to be larger.")

/* BANKED CALL, Phase 6: two independent checks for FamiStudio's domain,
 * for two different failure modes:
 *
 * 1. Did FSCODE (this file's hardcoded segment name) actually match
 *    CMakeLists.txt's FAMISTUDIO_SEGMENT cache variable's value? SIZEOF()
 *    being nonzero is the only way to tell from in here -- if the names
 *    silently drifted apart, this SECTIONS rule captures nothing,
 *    FamiStudio's real content falls back to wherever unmatched xo65 input
 *    lands by default (ordinary switchable content), and Call<famistudio_*>
 *    would be resolving a bank number for a domain that's actually empty.
 *    Zero IS tolerated via the same DEFINED() guard as OversizedFuncHigh's
 *    check just above -- confirmed with a real build (not assumed, contrary
 *    to what this comment used to claim) that CMake's own internal
 *    compiler-identification try_compile probes also pick up
 *    CMAKE_CXX_FLAGS (hence VRC1_LD), and link a bare `int main(){}` with
 *    no famistudio_pass1.o in sight -- the same bare-probe class Phase 4's
 *    analogous check exists for, not a demo-only exemption.
 * 2. Same real-bank-arithmetic class of check as window_test/bank3/oversized
 *    above: bank_layout<famistudio_tag>::section() (audio.cpp) hand-enters
 *    bank 6 -- must match prg_rom_famistudio's own ORIGIN. */
PROVIDE(__famistudio_domain_size = SIZEOF(FSCODE));
ASSERT(__famistudio_domain_size > 0 || !DEFINED(_Z17OversizedFuncHighv),
       "FSCODE captured no content -- CMakeLists.txt's FAMISTUDIO_SEGMENT cache variable no longer matches this file's own hardcoded FSCODE segment name (SECTIONS block above), so FamiStudio's real compiled code isn't landing in prg_rom_famistudio at all.")

PROVIDE(__banked_call_famistudio_rom_address = LOADADDR(FSCODE));
ASSERT(__banked_call_famistudio_rom_address == (6 * 0x10000) + 0xc000,
       "bank_layout<famistudio_tag>::section() in audio.cpp is out of sync with prg_rom_famistudio's ORIGIN in link.ld -- update the hand-entered literal to match.")

/* Only the regions THIS project owns need FULL()'d here: prg_rom_fixed and
 * chr_rom are declared in vrc1-helper.ld, but OUTPUT_FORMAT is additive
 * across INCLUDEd files (confirmed empirically -- see vrc1-helper.ld's own
 * OUTPUT_FORMAT comment) and every region name is in one shared namespace
 * by the time the whole script is assembled, so referencing them here,
 * AFTER this project's own domains, is what actually controls final file
 * order (prg_rom_fixed must stay last among PRG-ROM content, matching real
 * VRC1 hardware always showing the true last bank there). */
OUTPUT_FORMAT {
  FULL(prg_rom_window_test)
  FULL(prg_rom_bank3)
  FULL(prg_rom_oversized)
  FULL(prg_rom_famistudio)
  FULL(prg_rom_fixed)
  FULL(chr_rom)
}
