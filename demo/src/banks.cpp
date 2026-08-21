#include "banks.hpp"

#ifdef TARGET_NES
[[gnu::noinline]] FIXED void CallInLevelGraphics(void (*fn)(void*), void* ctx) {
    const u8 saved = mmc3::window2Control.get();
    mmc3::SwitchBank(mmc3::window2Control, LevelGraphicsBank(0));
    fn(ctx);
    mmc3::window2Control.set(saved);
}
#endif
