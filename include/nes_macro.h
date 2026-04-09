#ifndef MACRO_H
#define MACRO_H

#define RESET int main

#define NMI                                         \
__attribute__((weak, interrupt_norecurse))       \
void nmi

#define IRQ                                         \
__attribute__((weak, interrupt_norecurse))       \
void irq

#endif