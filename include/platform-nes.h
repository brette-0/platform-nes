#ifndef PLATFORM_NES_LIBRARY_H
#define PLATFORM_NES_LIBRARY_H
#include <stdint.h>

#ifdef __JETBRAINS_IDE__
    // The IDE doesn't know what this is, so we tell it to treat
    // the attribute as "nothing" (whitespace).
    #define interrupt_norecurse
#endif

#ifdef TARGET_NES
#include "../include/nes_macro.h"
#define quit 0
#else
#include "../include/unified_macro.h"
extern int quit;
#endif

#include "types.h"
#include "reset.h"
#include "input.h"
#include "video.h"
#include "audio.h"

#endif // PLATFORM_NES_LIBRARY_H