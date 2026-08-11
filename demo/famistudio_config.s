; FamiStudio sound engine configuration -- a REAL, project-owned config
; file, not CMake-generated. Edit this directly for this project's own
; needs (a different game/song/SFX set would edit its own copy of this
; file, not a CMake cache variable).
;
; CMakeLists.txt reads most values back out of this file (regex-extracted)
; so audio.cpp's own #if FAMISTUDIO_CFG_* checks and the BANKED_EXTERN
; segment tag it uses to reach famistudio_update() etc. stay honest against
; whatever's actually configured here -- it never generates or owns this
; file's content. See BANKED_CALL_THEORY.txt's "RESOLVED: famistudio_config.s
; moves to the demo root as a real, directly-edited file" for the reasoning.
; NTSC/PAL support are the one exception: CMake's REGION variable (set from
; TARGET_PLATFORM, see CMakeLists.txt) is fed in below via -D, and this file
; derives FAMISTUDIO_CFG_NTSC_SUPPORT/PAL_SUPPORT from it rather than
; carrying its own separate literal -- CMakeLists.txt computes the identical
; values from that same REGION for its own compile definitions, so the two
; can't drift apart.

.define FAMISTUDIO_CA65_ZP_SEGMENT   ZEROPAGE
.define FAMISTUDIO_CA65_RAM_SEGMENT  BSS
; Deliberately underscore-free (not e.g. "FAMISTUDIO_CODE") -- ld.lld's XO65
; reader decodes ca65 segment names through its own underscore-based escape
; scheme, and rejects one outright ("unknown underscore escape"). Routed
; into its own dedicated bank by demo/link.ld's FSCODE region.
.define FAMISTUDIO_CA65_CODE_SEGMENT FSCODE

; REGION comes in from CMake (-D REGION=<0|1>, CMakeLists.txt's own REGION
; variable -- 0 for NTSC, 1 for PAL, set per TARGET_PLATFORM). The .ifndef
; fallback keeps this file directly assemblable standalone (ca65 on this
; file alone, no CMake in the loop) at its NTSC default, matching this
; file's own "real, project-owned config, not CMake-generated" charter above
; -- REGION only ever narrows which of these two FamiStudio itself still
; reads is 1, it never becomes the sole source of truth in place of this file.
.ifndef REGION
    REGION = 0
.endif

FAMISTUDIO_CFG_EXTERNAL     = 1
FAMISTUDIO_CFG_NTSC_SUPPORT = 1 - REGION
FAMISTUDIO_CFG_PAL_SUPPORT  = REGION
FAMISTUDIO_CFG_DPCM_SUPPORT = 1
FAMISTUDIO_CFG_SFX_SUPPORT  = 1
FAMISTUDIO_CFG_SFX_STREAMS  = 2
FAMISTUDIO_USE_VOLUME_TRACK = 1

.include "famistudio_ca65.s"
