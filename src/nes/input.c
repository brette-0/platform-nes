#include <stdint.h>
#include "../../include/platform-nes/input.h"
void PollControllers(uint8_t* port1, uint8_t* port2) {
    IO_PORT1 = 1;
    IO_PORT1 = 0;
    *port1 = 0;
    *port2 = 0;

#pragma unroll
    for (int i = 0; i < 8; i++) {
        *port1 |= IO_PORT1;
        *port2 |= IO_PORT2;
        *port1 <<= 1;
        *port2 <<= 1;
    }
}
