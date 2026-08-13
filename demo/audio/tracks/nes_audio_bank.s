; Places the exported FamiStudio song data into its own PRG-ROM bank.
;
; demo/audio/tracks/nes_audio.s is a GENERATED artifact (re-exported from
; demo/nes.fms on every build, see CMakeLists.txt) and carries no .segment
; directive of its own, so its data would otherwise land in ca65's default
; CODE segment and be merged into ordinary .text. This wrapper is the seam:
; it opens the target segment and pulls the generated file in, leaving the
; generator's output untouched and re-exportable.
;
; _pprg__rom__003 decodes to the ELF section ".prg_rom_003" -- ld.lld's XO65
; reader reads '_' as an escape ('_p' -> '.', '__' -> a literal '_'); see
; demo/famistudio_config.s for the full escape table. No _fx here, unlike the
; engine's own segment: this is data, not code.
;
; That section is bank 2, mapped through WINDOW 2 ($A000, R7) -- deliberately
; a different window from the engine's own bank 1, since the engine walks
; this data while it executes and both must be mapped at once. See
; demo/src/banks.hpp's bank003_tag.
;
; CMakeLists.txt excludes nes_audio.s itself from the assembly glob so it is
; assembled ONLY through this file -- the same treatment famistudio_config.s
; already gets. Assembling both would define every symbol twice.

.segment "_pprg__rom__003"
.include "nes_audio.s"
