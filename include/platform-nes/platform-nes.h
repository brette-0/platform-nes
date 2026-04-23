#ifndef PLATFORM_NES_LIBRARY_H
#define PLATFORM_NES_LIBRARY_H
#include <stdint.h>

#if defined(LANDSCAPE) && defined(PORTRAIT)
#error "Must not define both Mirroring"
#endif

#if !defined(LANDSCAPE) && !defined(PORTRAIT)
#error "Must define Mirroring"
#endif



#ifndef __MOS__
    #define interrupt_norecurse
#endif

#ifdef TARGET_NES
#include "nes_macro.h"
#define quit 0
#else
#include "SDL3_macro.h"
extern int quit;
#endif

#include "types.h"
#include "technology.h"
#include "reset.h"
#include "input.h"
#include "video.h"
#include "audio.h"
#include "interrupts.h"

#endif // PLATFORM_NES_LIBRARY_H