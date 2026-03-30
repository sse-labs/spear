/*
 * Copyright (c) 2026 Maximilian Krebs
 * All rights reserved.
 */


#include <iostream>
#include <cstdint>

volatile int g_seed = 13;
volatile int g_sink = 0;

static inline int opaque(int x, int salt) {
    x = x * 1664525 + 1013904223 + salt;
    x ^= (x >> 13);
    x ^= (x << 7);
    x ^= (x >> 17);
    return x & 0x7fffffff;
}

int benchmark_function(int input) {
    int acc = input;

    // ------------------------------------------------------------------
    // Large acyclic region 1: many diamonds / branches, no loops
    // ------------------------------------------------------------------
    if ((opaque(acc, 1) & 1) == 0) {
        acc += 11;
    } else {
        acc += 3;
    }

    if ((opaque(acc, 2) & 2) == 0) {
        acc += 17;
    } else {
        acc += 5;
    }

    if ((opaque(acc, 3) % 3) == 0) {
        acc += 13;
    } else {
        acc += 7;
    }

    if ((opaque(acc, 4) % 5) < 2) {
        acc += 19;
    } else {
        acc += 2;
    }

    if ((opaque(acc, 5) & 4) != 0) {
        acc += 23;
    } else {
        acc += 1;
    }

    if ((opaque(acc, 6) % 7) == 0) {
        acc += 29;
    } else {
        acc += 4;
    }

    if ((opaque(acc, 7) & 8) != 0) {
        acc += 31;
    } else {
        acc += 6;
    }

    if ((opaque(acc, 8) % 4) == 1) {
        acc += 37;
    } else {
        acc += 8;
    }

    if ((opaque(acc, 9) % 6) < 3) {
        acc += 41;
    } else {
        acc += 9;
    }

    if ((opaque(acc, 10) & 16) != 0) {
        acc += 43;
    } else {
        acc += 10;
    }

    // ------------------------------------------------------------------
    // Loop 1: local loop that clustered approach can solve separately
    // ------------------------------------------------------------------
    for (int i = 0; i < 40; ++i) {
        if ((opaque(acc + i, 20) & 1) == 0) {
            acc += 12;
        } else {
            acc += 4;
        }

        if ((opaque(acc + i, 21) % 3) == 0) {
            acc += 15;
        } else {
            acc += 5;
        }

        if ((opaque(acc + i, 22) % 5) < 2) {
            acc += 18;
        } else {
            acc += 6;
        }
    }

    // ------------------------------------------------------------------
    // Large acyclic region 2
    // ------------------------------------------------------------------
    if ((opaque(acc, 30) & 1) == 0) {
        acc += 14;
    } else {
        acc += 2;
    }

    if ((opaque(acc, 31) & 2) == 0) {
        acc += 16;
    } else {
        acc += 3;
    }

    if ((opaque(acc, 32) % 3) == 1) {
        acc += 21;
    } else {
        acc += 4;
    }

    if ((opaque(acc, 33) % 5) == 2) {
        acc += 24;
    } else {
        acc += 7;
    }

    if ((opaque(acc, 34) & 4) != 0) {
        acc += 27;
    } else {
        acc += 8;
    }

    if ((opaque(acc, 35) % 7) < 3) {
        acc += 30;
    } else {
        acc += 9;
    }

    if ((opaque(acc, 36) & 8) != 0) {
        acc += 33;
    } else {
        acc += 10;
    }

    if ((opaque(acc, 37) % 4) == 0) {
        acc += 36;
    } else {
        acc += 11;
    }

    if ((opaque(acc, 38) % 6) == 5) {
        acc += 39;
    } else {
        acc += 12;
    }

    if ((opaque(acc, 39) & 16) != 0) {
        acc += 42;
    } else {
        acc += 13;
    }

    if ((opaque(acc, 40) % 9) < 4) {
        acc += 45;
    } else {
        acc += 14;
    }

    if ((opaque(acc, 41) & 32) != 0) {
        acc += 48;
    } else {
        acc += 15;
    }

    // ------------------------------------------------------------------
    // Loop 2: another local loop
    // ------------------------------------------------------------------
    for (int j = 0; j < 35; ++j) {
        if ((opaque(acc + j, 50) % 4) == 0) {
            acc += 20;
        } else {
            acc += 7;
        }

        if ((opaque(acc + j, 51) & 2) != 0) {
            acc += 22;
        } else {
            acc += 8;
        }
    }

    // ------------------------------------------------------------------
    // Large acyclic region 3
    // ------------------------------------------------------------------
    if ((opaque(acc, 60) & 1) == 0) {
        acc += 17;
    } else {
        acc += 1;
    }

    if ((opaque(acc, 61) % 3) == 0) {
        acc += 19;
    } else {
        acc += 2;
    }

    if ((opaque(acc, 62) % 5) < 2) {
        acc += 23;
    } else {
        acc += 3;
    }

    if ((opaque(acc, 63) & 4) != 0) {
        acc += 29;
    } else {
        acc += 4;
    }

    if ((opaque(acc, 64) % 7) == 0) {
        acc += 31;
    } else {
        acc += 5;
    }

    if ((opaque(acc, 65) & 8) != 0) {
        acc += 37;
    } else {
        acc += 6;
    }

    if ((opaque(acc, 66) % 4) == 1) {
        acc += 41;
    } else {
        acc += 7;
    }

    if ((opaque(acc, 67) % 6) < 3) {
        acc += 43;
    } else {
        acc += 8;
    }

    if ((opaque(acc, 68) & 16) != 0) {
        acc += 47;
    } else {
        acc += 9;
    }

    if ((opaque(acc, 69) % 8) == 2) {
        acc += 53;
    } else {
        acc += 10;
    }

    // ------------------------------------------------------------------
    // Loop 3: nested branching, but still a local loop cluster
    // ------------------------------------------------------------------
    for (int k = 0; k < 30; ++k) {
        if ((opaque(acc + k, 70) & 1) == 0) {
            acc += 9;
        } else {
            acc += 3;
        }

        if ((opaque(acc + k, 71) % 3) == 1) {
            acc += 14;
        } else {
            acc += 4;
        }

        if ((opaque(acc + k, 72) % 5) == 2) {
            acc += 25;
        } else {
            acc += 6;
        }

        if ((opaque(acc + k, 73) & 2) != 0) {
            acc += 11;
        } else {
            acc += 2;
        }
    }

    // ------------------------------------------------------------------
    // Large acyclic region 4
    // ------------------------------------------------------------------
    if ((opaque(acc, 80) & 1) == 0) {
        acc += 12;
    } else {
        acc += 1;
    }

    if ((opaque(acc, 81) & 2) == 0) {
        acc += 18;
    } else {
        acc += 2;
    }

    if ((opaque(acc, 82) % 3) == 2) {
        acc += 24;
    } else {
        acc += 3;
    }

    if ((opaque(acc, 83) % 5) == 1) {
        acc += 28;
    } else {
        acc += 4;
    }

    if ((opaque(acc, 84) & 4) != 0) {
        acc += 32;
    } else {
        acc += 5;
    }

    if ((opaque(acc, 85) % 7) < 3) {
        acc += 36;
    } else {
        acc += 6;
    }

    if ((opaque(acc, 86) & 8) != 0) {
        acc += 40;
    } else {
        acc += 7;
    }

    if ((opaque(acc, 87) % 4) == 0) {
        acc += 44;
    } else {
        acc += 8;
    }

    if ((opaque(acc, 88) % 6) == 5) {
        acc += 48;
    } else {
        acc += 9;
    }

    if ((opaque(acc, 89) & 16) != 0) {
        acc += 52;
    } else {
        acc += 10;
    }

    return acc;
}

int main() {
    int x = g_seed;
    x = benchmark_function(x);
    g_sink = x;
    std::cout << g_sink << '\n';
    return 0;
}