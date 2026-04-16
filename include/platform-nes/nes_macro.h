#ifndef MACRO_H
#define MACRO_H

#define RESET int main

#define NMI                             \
__attribute__((used, interrupt_norecurse))    \
void nmi

#endif