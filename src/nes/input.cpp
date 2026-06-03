#include <cstdint>
#include <platform-nes/input.hpp>
void PollControllers(std::uint8_t* port1, std::uint8_t* port2) {
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
