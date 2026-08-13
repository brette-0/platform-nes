; Places the FamiStudio sound-effect export into the audio DATA bank.
;
; Same seam, and the same reason, as demo/audio/tracks/nes_audio_bank.s:
; nes_sfx.s is FamiStudio's own export (regenerated from demo/nes.fms once
; the project's SFX bank is sorted out -- see CMakeLists.txt) and carries no
; .segment of its own, so its data would land in ca65's default CODE segment
; and be merged into ordinary resident .text.
;
; THAT IS NOT MERELY UNTIDY, IT IS BROKEN: src/nes/audio.cpp reaches the
; engine through mmc3::CallPairedBlock, which maps the engine's bank over
; $8000-$9FFF and this data bank over $A000-$BFFF for the duration of the
; call. Everything ordinarily resident in $8000-$BFFF is therefore NOT
; mapped while the engine runs -- so SFX data left in .text would be read as
; whatever the music bank happens to hold at that offset. Caught exactly that
; way: `sounds` had landed at $A9B6, inside the window the music data
; displaces.
;
; _pprg__rom__003 decodes to section ".prg_rom_003" (ld.lld XO65 escapes:
; _p -> '.', __ -> '_'); no _fx, this is data.

.segment "_pprg__rom__003"
.include "nes_sfx.s"
