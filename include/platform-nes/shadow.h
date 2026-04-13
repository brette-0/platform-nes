#ifndef SHADOW_H
#define SHADOW_H

#include <stdint.h>

extern uint8_t SPPUCRTL;
extern uint16_t xScroll;
extern uint16_t yScroll;

#define GEN_SHADOW_REGISTERS    \
    uint8_t  SPPUCRTL;          \
    uint16_t xScroll;           \
    uint16_t yScroll;

#endif