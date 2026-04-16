#ifndef INPUT_H
#define INPUT_H

#include <stdint.h>
#ifdef TARGET_NES
#ifndef IO_PORT1
#define IO_PORT1 *(volatile uint8_t*)0x4016
#define IO_PORT2 *(volatile uint8_t*)0x4017
#endif
#endif

enum Buttons {
    A       = 0x01,
    B       = 0x02,
    SELECT  = 0x04,
    START   = 0x08,
    UP      = 0x10,
    DOWN    = 0x20,
    LEFT    = 0x40,
    RIGHT   = 0x80,
};

void PollControllers(uint8_t* port1, uint8_t* port2);

#endif //INPUT_H