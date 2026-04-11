#ifndef MACRO_H
#define MACRO_H

#include <platform-nes/shadow.h>

#define RESET                   \
    void __main();              \
    GEN_SHADOW_REGISTERS        \
    int main(){                 \
        __main();               \
    }                           \
    inline void __main

#define NMI                             \
__attribute__((used, interrupt_norecurse))    \
void nmi

#define IRQ                             \
__attribute__((used, interrupt_norecurse))    \
void irq

#endif