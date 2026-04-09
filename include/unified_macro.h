#ifndef MACRO_H
#define MACRO_H

#include "audio.h"

extern void init(void);
extern void post(void);

#define RESET                   \
    static void usr_main();     \
    int main(){                 \
        init();                 \
        usr_main();             \
        post();                 \
    }                           \
    inline static void usr_main


#define NMI                     \
    static void __nmi();        \
    void nmi() {                \
        __nmi();                \
    }                           \
    inline static void __nmi
#endif