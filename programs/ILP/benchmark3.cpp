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

    // ---------------------------------------------------------------
    // Acyclic prefix: some global structure, but still moderate
    // ---------------------------------------------------------------
    if ((opaque(input, 1) & 1) == 0) {
        acc += 11;
    } else {
        acc += 3;
    }

    if ((opaque(input, 2) % 3) == 0) {
        acc += 17;
    } else {
        acc += 5;
    }

    if ((opaque(input, 3) % 5) < 2) {
        acc += 23;
    } else {
        acc += 7;
    }

    if ((opaque(input, 4) & 4) != 0) {
        acc += 29;
    } else {
        acc += 9;
    }

    if ((opaque(input, 5) % 7) == 0) {
        acc += 31;
    } else {
        acc += 10;
    }

    if ((opaque(input, 6) & 8) != 0) {
        acc += 37;
    } else {
        acc += 12;
    }

    // ---------------------------------------------------------------
    // Loop cluster 1
    //
    // Important design choice:
    // This loop uses only local state (l1), derived from input.
    // It does NOT depend on acc or any other loop cluster.
    // ---------------------------------------------------------------
    int l1 = opaque(input, 100);

    for (int i = 0; i < 48; ++i) {
        if ((opaque(l1 + i, 101) & 1) == 0) {
            l1 += 12;
        } else {
            l1 += 4;
        }

        if ((opaque(l1 + i, 102) % 3) == 0) {
            l1 += 19;
        } else {
            l1 += 6;
        }

        if ((opaque(l1 + i, 103) % 5) < 2) {
            l1 += 27;
        } else {
            l1 += 8;
        }

        if ((opaque(l1 + i, 104) & 2) != 0) {
            l1 += 15;
        } else {
            l1 += 5;
        }
    }

    // ---------------------------------------------------------------
    // Acyclic bridge 1
    //
    // Uses only input again, so it does not couple loop clusters.
    // ---------------------------------------------------------------
    if ((opaque(input, 10) & 1) == 0) {
        acc += 13;
    } else {
        acc += 2;
    }

    if ((opaque(input, 11) & 2) == 0) {
        acc += 21;
    } else {
        acc += 4;
    }

    if ((opaque(input, 12) % 3) == 1) {
        acc += 25;
    } else {
        acc += 6;
    }

    if ((opaque(input, 13) % 5) == 2) {
        acc += 33;
    } else {
        acc += 8;
    }

    if ((opaque(input, 14) & 4) != 0) {
        acc += 39;
    } else {
        acc += 10;
    }

    if ((opaque(input, 15) % 7) < 3) {
        acc += 41;
    } else {
        acc += 11;
    }

    // ---------------------------------------------------------------
    // Loop cluster 2
    // Again completely local.
    // ---------------------------------------------------------------
    int l2 = opaque(input, 200);

    for (int j = 0; j < 44; ++j) {
        if ((opaque(l2 + j, 201) % 4) == 0) {
            l2 += 18;
        } else {
            l2 += 7;
        }

        if ((opaque(l2 + j, 202) & 2) != 0) {
            l2 += 22;
        } else {
            l2 += 9;
        }

        if ((opaque(l2 + j, 203) % 6) < 3) {
            l2 += 31;
        } else {
            l2 += 10;
        }

        if ((opaque(l2 + j, 204) & 8) != 0) {
            l2 += 14;
        } else {
            l2 += 3;
        }
    }

    // ---------------------------------------------------------------
    // Acyclic bridge 2
    // Still uncoupled from loops.
    // ---------------------------------------------------------------
    if ((opaque(input, 20) & 1) == 0) {
        acc += 14;
    } else {
        acc += 1;
    }

    if ((opaque(input, 21) % 3) == 0) {
        acc += 20;
    } else {
        acc += 2;
    }

    if ((opaque(input, 22) % 5) < 2) {
        acc += 24;
    } else {
        acc += 3;
    }

    if ((opaque(input, 23) & 4) != 0) {
        acc += 30;
    } else {
        acc += 4;
    }

    if ((opaque(input, 24) % 7) == 0) {
        acc += 34;
    } else {
        acc += 5;
    }

    if ((opaque(input, 25) & 8) != 0) {
        acc += 38;
    } else {
        acc += 6;
    }

    if ((opaque(input, 26) % 4) == 1) {
        acc += 42;
    } else {
        acc += 7;
    }

    // ---------------------------------------------------------------
    // Loop cluster 3
    // Local again.
    // ---------------------------------------------------------------
    int l3 = opaque(input, 300);

    for (int k = 0; k < 40; ++k) {
        if ((opaque(l3 + k, 301) & 1) == 0) {
            l3 += 9;
        } else {
            l3 += 3;
        }

        if ((opaque(l3 + k, 302) % 3) == 1) {
            l3 += 16;
        } else {
            l3 += 4;
        }

        if ((opaque(l3 + k, 303) % 5) == 2) {
            l3 += 28;
        } else {
            l3 += 6;
        }

        if ((opaque(l3 + k, 304) & 2) != 0) {
            l3 += 13;
        } else {
            l3 += 5;
        }

        if ((opaque(l3 + k, 305) % 7) < 3) {
            l3 += 21;
        } else {
            l3 += 8;
        }
    }

    // ---------------------------------------------------------------
    // Acyclic bridge 3
    // ---------------------------------------------------------------
    if ((opaque(input, 30) & 1) == 0) {
        acc += 15;
    } else {
        acc += 2;
    }

    if ((opaque(input, 31) & 2) == 0) {
        acc += 19;
    } else {
        acc += 3;
    }

    if ((opaque(input, 32) % 3) == 2) {
        acc += 27;
    } else {
        acc += 5;
    }

    if ((opaque(input, 33) % 5) == 1) {
        acc += 35;
    } else {
        acc += 7;
    }

    if ((opaque(input, 34) & 4) != 0) {
        acc += 43;
    } else {
        acc += 9;
    }

    if ((opaque(input, 35) % 7) < 3) {
        acc += 47;
    } else {
        acc += 10;
    }

    // ---------------------------------------------------------------
    // Loop cluster 4
    // Local again.
    // ---------------------------------------------------------------
    int l4 = opaque(input, 400);

    for (int m = 0; m < 36; ++m) {
        if ((opaque(l4 + m, 401) % 4) == 0) {
            l4 += 17;
        } else {
            l4 += 6;
        }

        if ((opaque(l4 + m, 402) & 2) != 0) {
            l4 += 24;
        } else {
            l4 += 8;
        }

        if ((opaque(l4 + m, 403) % 6) == 5) {
            l4 += 29;
        } else {
            l4 += 9;
        }

        if ((opaque(l4 + m, 404) & 16) != 0) {
            l4 += 11;
        } else {
            l4 += 4;
        }

        if ((opaque(l4 + m, 405) % 5) < 2) {
            l4 += 26;
        } else {
            l4 += 7;
        }
    }

    // ---------------------------------------------------------------
    // Final merge
    //
    // The loop clusters contribute only here.
    // This is exactly the structure you want:
    // many expensive local loop regions, but additive merge only.
    // ---------------------------------------------------------------
    acc += (l1 & 255);
    acc += (l2 & 255);
    acc += (l3 & 255);
    acc += (l4 & 255);

    // ---------------------------------------------------------------
    // Acyclic suffix
    // ---------------------------------------------------------------
    if ((opaque(input, 40) & 1) == 0) {
        acc += 12;
    } else {
        acc += 1;
    }

    if ((opaque(input, 41) % 3) == 0) {
        acc += 18;
    } else {
        acc += 2;
    }

    if ((opaque(input, 42) % 5) < 2) {
        acc += 22;
    } else {
        acc += 3;
    }

    if ((opaque(input, 43) & 4) != 0) {
        acc += 28;
    } else {
        acc += 4;
    }

    if ((opaque(input, 44) % 7) == 0) {
        acc += 32;
    } else {
        acc += 5;
    }

    if ((opaque(input, 45) & 8) != 0) {
        acc += 36;
    } else {
        acc += 6;
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