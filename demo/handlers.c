#include "handlers.h"
#include <platform-nes/video.h>

#include "platform-nes/audio.h"

extern volatile uint8_t spriteZeroHandled;
extern uint16_t xWorldSpace;

void SpriteZeroHandler(void) {
    SetColorPriority(RED);
    spriteZeroHandled = 1;
    SetScroll(xWorldSpace, 16);
#ifdef TARGET_NES
    AudioUpdate();
#endif
    SetColorPriority(0);
}