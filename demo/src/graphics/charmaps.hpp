#pragma once

#include <platform-nes/technology.hpp>

// Default character map: source token -> CHR tile byte.
//
// CHARMAP expands to a constexpr function `charmap_generic(char)`. Any TU that
// maps a string through `generic` (via STRING) needs this function in scope, so
// it includes this header (strings.hpp pulls it in for that reason). Being
// constexpr (hence inline), the charmap can be included by any number of TUs
// with no ODR/LTO trouble.
CHARMAP(generic,                            // default char map
    CM(0, 0x00)
    CM(1, 0x01)
    CM(2, 0x02)
    CM(3, 0x03)
    CM(4, 0x04)
    CM(5, 0x05)
    CM(6, 0x06)
    CM(7, 0x07)
    CM(8, 0x08)
    CM(9, 0x09)
    CM(A, 0xe1)
    CM(B, 0xe2)
    CM(C, 0xe3)
    CM(D, 0xe4)
    CM(E, 0xe5)
    CM(F, 0xe6)
    CM(G, 0xe7)
    CM(H, 0xe8)
    CM(I, 0xe9)
    CM(J, 0xea)
    CM(K, 0xeb)
    CM(L, 0xec)
    CM(M, 0xed)
    CM(N, 0xee)
    CM(O, 0xef)
    CM(P, 0xf0)
    CM(Q, 0xf1)
    CM(R, 0xf2)
    CM(S, 0xf3)
    CM(T, 0xf4)
    CM(U, 0xf5)
    CM(W, 0xf6)
    CM(V, 0xf7)
    CM(X, 0xf8)
    CM(Y, 0xf9)
    CM(Z, 0xfa)
);
