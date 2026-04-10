#ifndef PLATFORM_NES_LIBRARY_H
#define PLATFORM_NES_LIBRARY_H
#include <stdint.h>

#if defined(LANDSCAPE) && defined(PORTRAIT)
#error "Must not define both Mirroring"
#endif

#if !defined(LANDSCAPE) && !defined(PORTRAIT)
#error "Must define Mirroring"
#endif



#ifdef __JETBRAINS_IDE__
    // The IDE doesn't know what this is, so we tell it to treat
    // the attribute as "nothing" (whitespace).
    #define interrupt_norecurse
#endif

#ifdef TARGET_NES
#include "nes_macro.h"
#define quit 0
#else
#include "unified_macro.h"
extern int quit;
#endif

#include "types.h"
#include "reset.h"
#include "input.h"
#include "video.h"
#include "audio.h"

#endif // PLATFORM_NES_LIBRARY_H