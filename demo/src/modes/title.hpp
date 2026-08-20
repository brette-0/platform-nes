#pragma once
#include "platform-nes/technology.hpp"

namespace title {
    void main();

    void nmi_handler();
    void irq_handler();
}
