#include <intsh>
using namespace br0::intsh;
#include <platform-nes/input.hpp>
void PollControllers(u8* port1, u8* port2) {
    IO_PORT1 = 1;
    IO_PORT1 = 0;
    *port1 = 0;
    *port2 = 0;

#pragma unroll
    for (auto i = 0; i < 8; i++) {
        *port1 |= (IO_PORT1 & 1) << i;
        *port2 |= (IO_PORT2 & 1) << i;
    }
}
