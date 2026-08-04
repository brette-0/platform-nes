; FamiStudio sound engine configuration -- a REAL, project-owned config
; file, not CMake-generated. Edit this directly for this project's own
; needs (a different game/song/SFX set would edit its own copy of this
; file, not a CMake cache variable).
;
; CMakeLists.txt only ever READS values back out of this file (regex-
; extracted) so audio.cpp's own #if FAMISTUDIO_CFG_* checks and the
; BANKED_EXTERN segment tag it uses to reach famistudio_update() etc. stay
; honest against whatever's actually configured here -- it never generates
; or owns this file's content. See BANKED_CALL_THEORY.txt's "RESOLVED:
; famistudio_config.s moves to the demo root as a real, directly-edited
; file" for the reasoning.

.define FAMISTUDIO_CA65_ZP_SEGMENT   ZEROPAGE
.define FAMISTUDIO_CA65_RAM_SEGMENT  BSS
; Deliberately underscore-free (not e.g. "FAMISTUDIO_CODE") -- ld.lld's XO65
; reader decodes ca65 segment names through its own underscore-based escape
; scheme, and rejects one outright ("unknown underscore escape"). Routed
; into its own dedicated bank by demo/link.ld's FSCODE region.
.define FAMISTUDIO_CA65_CODE_SEGMENT FSCODE

FAMISTUDIO_CFG_EXTERNAL     = 1
FAMISTUDIO_CFG_NTSC_SUPPORT = 1
FAMISTUDIO_CFG_PAL_SUPPORT  = 0
FAMISTUDIO_CFG_DPCM_SUPPORT = 1
FAMISTUDIO_CFG_SFX_SUPPORT  = 1
FAMISTUDIO_CFG_SFX_STREAMS  = 2
FAMISTUDIO_USE_VOLUME_TRACK = 1

.include "famistudio_ca65.s"
