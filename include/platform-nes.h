#ifndef PLATFORM_NES_LIBRARY_H
#define PLATFORM_NES_LIBRARY_H
#include <stdint.h>

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

#endif // PLATFORM_NES_LIBRARY_H