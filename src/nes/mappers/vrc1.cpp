#include <platform-nes/mappers/vrc1.hpp>

tech::wo_register<0x8000> VRC1::window1Control;
tech::wo_register<0xa000> VRC1::window2Control;
tech::wo_register<0xc000> VRC1::window3Control;

extern "C" void _start();

extern "C" FIXED void _reset() {
    tech::poke(0x8000, 0);
    tech::poke(0xa000, 1);
    tech::poke(0xc000, 2);
    _start();
}

/*
 * BANKED CALL, Phase 1 smoke test: exercises VRC1_BANKED()+Call<Fn> through
 * the always-mapped fixed-bank path (bank_layout<fixed_bank_tag>::always_mapped
 * == true, so this collapses to a bare call at runtime -- Call<Fn>'s
 * non-fixed branch isn't reachable until a real bank_layout<Tag>
 * specialization exists, Phase 2+). No existing ::FIXED-tagged function had
 * a real C++ call site to convert instead: ::VRC1::Long and
 * VRC1::Detail::CallInWindow are templates, out of VRC1_BANKED()'s scope (see
 * BANKED_CALL_THEORY.txt); ::_reset is a raw reset-vector target with no
 * C++ caller to route through Call<Fn>. Remove once a real VRC1_BANKED()-tagged
 * fixed-bank function exists to stand in for it.
 *
 * noinline alone isn't enough: it blocks inlining, but a body with no
 * observable effect is still legally removable by DCE, taking the call
 * with it -- confirmed empirically, an empty noinline body vanished
 * entirely at link time, silently testing nothing. The volatile write
 * below is what actually forces this call to survive to the final link.
 */
VRC1_BANKED(".prg_rom_fixed", fixed_bank, void, BankedCallSmokeTest);

volatile u8 bankedCallSmokeTestMarker = 0;

[[gnu::noinline]] void BankedCallSmokeTest() {
    bankedCallSmokeTestMarker = 1;
}

/*
 * BANKED CALL, Phase 2 smoke test: exercises Call<Fn> through a REAL
 * switchable-window round trip (VRC1::Detail::CallInSection), not just the
 * always-mapped short-circuit Phase 1 covered.
 *
 * window_test_tag's section() IS a fixed, hand-picked address (0xdfc0,
 * window index 2 / window3Control) -- a real constexpr literal, matching
 * prg_rom_window_test's ORIGIN in vrc1.ld (see that region's own comment
 * for why: an earlier revision computed this at runtime from a linker
 * symbol instead, measured at ~120 bytes of pure overhead per Call<Fn>
 * call site, since the optimizer never manages to fold the resulting
 * subtract/divide/branch back down to the compile-time constant it
 * actually is). vrc1.ld's ASSERT catches the two going out of sync.
 */
struct window_test_tag {};

template <> struct VRC1::bank_layout<window_test_tag> {
    static constexpr bool always_mapped = false;
    static constexpr section_t section() {
        return { 0xdfc0, 6 };
    }
};

VRC1_BANKED(".banked_call_window_test", window_test, void, BankedCallWindowTest);

volatile u8 bankedCallWindowTestMarker = 0;

[[gnu::noinline]] void BankedCallWindowTest() {
    bankedCallWindowTestMarker = 1;
}

/*
 * BANKED CALL, Phase 3 smoke test: exercises a REAL multi-bank window --
 * window3Control now has TWO distinct physical banks that can appear at
 * $C000-$DFFF (bank 2, window_test_tag's default bank above, and bank 3,
 * here), so CallInSection has to write the CORRECT one each time instead of
 * reasserting the one bank that was always right (::VRC1::Long's own doc
 * comment on the project's old degenerate assumption). See PRG-ROM growing
 * to 40 KiB and prg_rom_bank3 in vrc1.ld for the ORIGIN-encoding this
 * depends on.
 *
 * bank3_test_tag's section() hand-encodes bank 3 directly into rom_address's
 * high bits ((3 << 16) | 0xc000) -- CallInSection extracts it back out with
 * rom_address >> 16 (see its own comment) -- still a real constexpr literal,
 * so this pays the same ~0 overhead window_test_tag's fix established, not
 * the ~120-byte runtime-resolution tax.
 *
 * This function ALSO calls Call<BankedCallWindowTest>() from inside itself
 * -- a NESTED cross-bank call, both targeting window3Control but different
 * banks (3, then 2, then back to 3). This is the real test: it only comes
 * out correct if CallInSection's SHADOW-based save/restore threads through
 * a genuine BANK VALUE change on entry/exit, not just window-register
 * identity (Phase 2's test never exercised this, since window_test_tag was
 * the ONLY non-fixed tag in the domain and never nested into another one).
 * Getting the bank number wrong here doesn't just miss reasserting a value
 * that was already correct -- it means whichever bank is ACTUALLY switched
 * in doesn't contain the code control returns to, the first real case in
 * this design where that's true.
 */
struct bank3_test_tag {};

template <> struct VRC1::bank_layout<bank3_test_tag> {
    static constexpr bool always_mapped = false;
    static constexpr section_t section() {
        // (3u << 16) is WRONG here: unsigned int is only 16 bits wide on
        // this target, so that shift is undefined behavior (caught by
        // -Wshift-count-overflow) rather than the 0x30000 it looks like.
        // rom_address is u32 -- shift in that width explicitly.
        return { (static_cast<u32>(3) << 16) | 0xc000, 12 };
    }
};

VRC1_BANKED(".prg_rom_bank3", bank3_test, void, BankedCallBank3Test);

volatile u8 bankedCallBank3TestMarker = 0;

[[gnu::noinline]] void BankedCallBank3Test() {
    bankedCallBank3TestMarker = 3;
    VRC1::Call<BankedCallWindowTest>();
}

/*
 * BANKED CALL, Phase 4 smoke test: an OVERSIZED domain -- bigger than one
 * 8 KiB window, spanning window2Control AND window3Control simultaneously
 * (banks 4+5, $A000-$DFFF, see prg_rom_oversized in vrc1.ld). Unlike every
 * earlier tag, size here is NOT a hand-entered literal: it's genuinely
 * read from the linker's own SIZEOF(), because how big this domain gets is
 * exactly the fact nobody should have to declare in advance (that's the
 * whole point of the feature -- see BANKED_CALL_THEORY.txt). CallInSection
 * ceil-divides it to get a window count and, when that's >1, hands off to
 * CallInWindows2 (vrc1.hpp) instead of the single-register path.
 *
 * BUG, CAUGHT LATE (found via a real runtime crash, not this file's own
 * review): base bank was originally hand-entered as 5, matching a mental
 * "next free bank after bank3_test's 3" model that was never checked
 * against the REAL file layout FULL() actually produces (vrc1.ld). Real
 * banks are assigned by FILE OFFSET / 0x2000, in FULL()'s listed order --
 * prg_rom_oversized is the 4th PRG region listed, immediately after
 * prg_rom_bank3 (which ends exactly at file offset 0x8000 = the START of
 * bank 4), so its two halves are REALLY banks 4 and 5, not 5 and 6. Bank 6
 * is actually prg_rom_fixed's own real bank identity. Writing 6 into
 * window3Control (as the old, wrong base+1 did) therefore mapped in
 * prg_rom_fixed's content, not OversizedFuncHigh's -- genuine garbage
 * execution, not a data/marker mismatch a marker check would have caught.
 */
struct oversized_tag {};

extern "C" const u8 __oversized_domain_size[];

template <> struct VRC1::bank_layout<oversized_tag> {
    static constexpr bool always_mapped = false;
    static section_t section() {
        // Base bank (4) IS hand-entered and constexpr-safe -- it's the
        // domain's own fixed starting point, same as bank3_test_tag's.
        // Only size is a genuine runtime read; see this tag's own comment.
        return { (static_cast<u32>(4) << 16) | 0xa000, reinterpret_cast<u32>(__oversized_domain_size) };
    }
};

VRC1_BANKED(".prg_rom_oversized", oversized, void, OversizedFuncLow);
VRC1_BANKED(".prg_rom_oversized", oversized, void, OversizedFuncHigh);

volatile u8 oversizedFuncLowMarker = 0;
volatile u8 oversizedFuncHighMarker = 0;

/*
 * OversizedFuncLow calls OversizedFuncHigh as an ORDINARY, BARE C++ call --
 * no Call<Fn>, no BANKED_CALL machinery at all. This is the actual point of
 * an oversized domain: once Call<OversizedFuncLow>() has mapped BOTH its
 * windows in (CallInWindows2), everything inside the domain is just
 * ordinary, contiguous, normally-addressable code -- BANKED_CALL_THEORY.txt's
 * "intra-domain calls stay bare, always" rule. Getting CallInWindows2 wrong
 * (e.g. only switching one of the two windows, or switching the wrong bank
 * for the second one) wouldn't just reassert an already-correct value the
 * way Phase 2/3's tests could tolerate -- OversizedFuncHigh's JSR target
 * would show whatever ACTUALLY happens to be banked into window3Control at
 * the time, not necessarily bank 5's real code. (This is exactly what
 * happened when the base bank below was still wrong -- see this domain's
 * own comment in vrc1.ld.)
 */
[[gnu::noinline]] void OversizedFuncLow() {
    oversizedFuncLowMarker = 1;
    // Padding: inline asm .fill, INSIDE this function's own body -- not a
    // separate global array. A data array tagged into .prg_rom_oversized
    // conflicts with the section already being classified executable by
    // OversizedFuncLow/High themselves (confirmed: "section type conflict"
    // at compile time, GNU/Clang won't mix code and data flags on one
    // named section). Padding as code, inside an already-guaranteed-to-
    // survive function (this one is Call<>'d directly), sidesteps both
    // problems: no section-type mismatch, and no separate gnu::used/retain
    // needed to keep it from being dead-code-eliminated. Specifically
    // sized to push OversizedFuncHigh's own address past the 0x2000 (one
    // window) mark within the domain, so this test genuinely exercises two
    // windows instead of coincidentally fitting in one -- vrc1.ld's ASSERT
    // on __oversized_domain_size is the build-time guard confirming that.
    // 0xea is 6502 NOP -- never executed (falls after the call below), but
    // an innocuous choice of filler byte regardless.
    __asm__ volatile(".fill 8300, 1, 0xea");
    OversizedFuncHigh();
}

/*
 * Also nests a Call<Fn> into a DIFFERENT, smaller (single-window) domain
 * from partway through the oversized one, on top of the bare intra-domain
 * call above -- the save/restore composition case Phase 4's own roadmap
 * entry calls out: CallInWindows2's two SHADOW-based register switches
 * (window2Control AND window3Control) have to stay correctly saved while
 * this NESTED, single-window Call<> temporarily repurposes JUST
 * window3Control for BankedCallWindowTest's own bank (2), leaving
 * window2Control (still bank 4, this domain's own) completely untouched
 * throughout -- then restore window3Control back to bank 5, not whatever
 * BankedCallWindowTest happened to leave behind.
 */
[[gnu::noinline]] void OversizedFuncHigh() {
    oversizedFuncHighMarker = 1;
    VRC1::Call<BankedCallWindowTest>();
}

/**
 * @brief Re-syncs window1Control/2/3's RAM shadows to match what ::_reset
 *        already poked into hardware.
 *
 * Runs as an ordinary global constructor -- i.e. after crt0's .bss zeroing,
 * which is what makes writing through ::VRC1::SwitchBank safe here but not
 * in ::_reset (see its comment). By this point the switchable windows are
 * already correctly banked by hardware; this only brings the RAM shadows
 * (read by wo_register::get()) into agreement with them, so later code
 * that reads e.g. window2Control.get() sees 1, not a stale post-.bss-zero 0.
 */
__attribute__((constructor(101)))
static void SyncBankShadows() {
    VRC1::SwitchBank(VRC1::window1Control, 0);
    VRC1::SwitchBank(VRC1::window2Control, 1);
    VRC1::SwitchBank(VRC1::window3Control, 2);
    VRC1::Call<BankedCallSmokeTest>();
    // Safe here specifically because it comes AFTER the three SwitchBank
    // calls above: CallInSection's SHADOW-based save/restore (inside
    // CallInWindow) needs window1/2/3Control's RAM shadows to already
    // reflect real hardware state, which is exactly what this function
    // exists to establish -- see this function's own doc comment.
    VRC1::Call<BankedCallWindowTest>();
    // Nested cross-bank call: switches window3Control to bank 3, runs,
    // and (from BankedCallBank3Test's own body) nests a Call<> back to
    // bank 2's BankedCallWindowTest before returning -- see
    // BankedCallBank3Test's own comment for why this is the real test.
    VRC1::Call<BankedCallBank3Test>();
    // Oversized domain: maps window2Control=bank4 AND window3Control=bank5
    // simultaneously (CallInWindows2), runs OversizedFuncLow, which bare-
    // calls OversizedFuncHigh (both windows still mapped, ordinary
    // addressing) and nests a single-window Call<> before either window
    // gets restored -- see OversizedFuncLow/High's own comments.
    VRC1::Call<OversizedFuncLow>();
}

tech::wo_register<0x9000> VRC1::chrHighBits;
tech::wo_register<0xe000> VRC1::chr0Control;
tech::wo_register<0xf000> VRC1::chr1Control;
