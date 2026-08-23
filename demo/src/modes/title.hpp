#pragma once
#include <platform-nes/technology.hpp>
#include <platform-nes/extras/text.hpp>

namespace title {
    void main();

    void nmi_handler();
    void irq_handler();

    void InitTitleScreen();
    u8 MenuAttributesProvider(u8 i);
}
