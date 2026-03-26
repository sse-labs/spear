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
    // Large acyclic prefix
    // Intentionally sizeable, but based only on input so it does not
    // couple the heavy loop clusters.
    // ---------------------------------------------------------------
    if ((opaque(input, 1) & 1) == 0) {
        acc += 11;
    } else {
        acc += 3;
    }

    if ((opaque(input, 2) & 2) == 0) {
        acc += 17;
    } else {
        acc += 5;
    }

    if ((opaque(input, 3) % 3) == 0) {
        acc += 19;
    } else {
        acc += 7;
    }

    if ((opaque(input, 4) % 5) < 2) {
        acc += 23;
    } else {
        acc += 9;
    }

    if ((opaque(input, 5) & 4) != 0) {
        acc += 29;
    } else {
        acc += 10;
    }

    if ((opaque(input, 6) % 7) == 0) {
        acc += 31;
    } else {
        acc += 11;
    }

    if ((opaque(input, 7) & 8) != 0) {
        acc += 37;
    } else {
        acc += 12;
    }

    if ((opaque(input, 8) % 4) == 1) {
        acc += 41;
    } else {
        acc += 13;
    }

    if ((opaque(input, 9) % 6) < 3) {
        acc += 43;
    } else {
        acc += 14;
    }

    if ((opaque(input, 10) & 16) != 0) {
        acc += 47;
    } else {
        acc += 15;
    }

    if ((opaque(input, 11) % 9) < 4) {
        acc += 53;
    } else {
        acc += 16;
    }

    if ((opaque(input, 12) & 32) != 0) {
        acc += 59;
    } else {
        acc += 17;
    }

    if ((opaque(input, 13) % 11) == 5) {
        acc += 61;
    } else {
        acc += 18;
    }

    if ((opaque(input, 14) & 64) != 0) {
        acc += 67;
    } else {
        acc += 19;
    }

    if ((opaque(input, 15) % 13) < 6) {
        acc += 71;
    } else {
        acc += 20;
    }

    if ((opaque(input, 16) % 8) == 2) {
        acc += 73;
    } else {
        acc += 21;
    }

    // ---------------------------------------------------------------
    // Loop cluster 1
    // Heavy, but completely local to l1.
    // ---------------------------------------------------------------
    int l1 = opaque(input, 100);

    for (int i = 0; i < 72; ++i) {
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

        if ((opaque(l1 + i, 105) % 7) < 3) {
            l1 += 21;
        } else {
            l1 += 7;
        }

        if ((opaque(l1 + i, 106) & 8) != 0) {
            l1 += 31;
        } else {
            l1 += 9;
        }

        if ((opaque(l1 + i, 107) % 4) == 1) {
            l1 += 17;
        } else {
            l1 += 3;
        }
    }

    // ---------------------------------------------------------------
    // Acyclic bridge 1
    // Uses only input.
    // ---------------------------------------------------------------
    if ((opaque(input, 20) & 1) == 0) {
        acc += 13;
    } else {
        acc += 2;
    }

    if ((opaque(input, 21) & 2) == 0) {
        acc += 21;
    } else {
        acc += 4;
    }

    if ((opaque(input, 22) % 3) == 1) {
        acc += 25;
    } else {
        acc += 6;
    }

    if ((opaque(input, 23) % 5) == 2) {
        acc += 33;
    } else {
        acc += 8;
    }

    if ((opaque(input, 24) & 4) != 0) {
        acc += 39;
    } else {
        acc += 10;
    }

    if ((opaque(input, 25) % 7) < 3) {
        acc += 41;
    } else {
        acc += 11;
    }

    if ((opaque(input, 26) & 8) != 0) {
        acc += 45;
    } else {
        acc += 12;
    }

    if ((opaque(input, 27) % 4) == 0) {
        acc += 49;
    } else {
        acc += 13;
    }

    if ((opaque(input, 28) % 6) == 5) {
        acc += 55;
    } else {
        acc += 14;
    }

    if ((opaque(input, 29) & 16) != 0) {
        acc += 57;
    } else {
        acc += 15;
    }

    // ---------------------------------------------------------------
    // Loop cluster 2
    // ---------------------------------------------------------------
    int l2 = opaque(input, 200);

    for (int j = 0; j < 68; ++j) {
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

        if ((opaque(l2 + j, 205) % 5) == 2) {
            l2 += 26;
        } else {
            l2 += 8;
        }

        if ((opaque(l2 + j, 206) & 16) != 0) {
            l2 += 35;
        } else {
            l2 += 11;
        }

        if ((opaque(l2 + j, 207) % 9) < 4) {
            l2 += 16;
        } else {
            l2 += 5;
        }
    }

    // ---------------------------------------------------------------
    // Acyclic bridge 2
    // ---------------------------------------------------------------
    if ((opaque(input, 30) & 1) == 0) {
        acc += 14;
    } else {
        acc += 1;
    }

    if ((opaque(input, 31) % 3) == 0) {
        acc += 20;
    } else {
        acc += 2;
    }

    if ((opaque(input, 32) % 5) < 2) {
        acc += 24;
    } else {
        acc += 3;
    }

    if ((opaque(input, 33) & 4) != 0) {
        acc += 30;
    } else {
        acc += 4;
    }

    if ((opaque(input, 34) % 7) == 0) {
        acc += 34;
    } else {
        acc += 5;
    }

    if ((opaque(input, 35) & 8) != 0) {
        acc += 38;
    } else {
        acc += 6;
    }

    if ((opaque(input, 36) % 4) == 1) {
        acc += 42;
    } else {
        acc += 7;
    }

    if ((opaque(input, 37) % 6) < 3) {
        acc += 46;
    } else {
        acc += 8;
    }

    if ((opaque(input, 38) & 16) != 0) {
        acc += 50;
    } else {
        acc += 9;
    }

    if ((opaque(input, 39) % 8) == 2) {
        acc += 54;
    } else {
        acc += 10;
    }

    // ---------------------------------------------------------------
    // Loop cluster 3
    // ---------------------------------------------------------------
    int l3 = opaque(input, 300);

    for (int k = 0; k < 64; ++k) {
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

        if ((opaque(l3 + k, 306) & 8) != 0) {
            l3 += 32;
        } else {
            l3 += 10;
        }

        if ((opaque(l3 + k, 307) % 4) == 0) {
            l3 += 18;
        } else {
            l3 += 7;
        }

        if ((opaque(l3 + k, 308) % 9) == 5) {
            l3 += 24;
        } else {
            l3 += 6;
        }
    }

    // ---------------------------------------------------------------
    // Acyclic bridge 3
    // ---------------------------------------------------------------
    if ((opaque(input, 40) & 1) == 0) {
        acc += 15;
    } else {
        acc += 2;
    }

    if ((opaque(input, 41) & 2) == 0) {
        acc += 19;
    } else {
        acc += 3;
    }

    if ((opaque(input, 42) % 3) == 2) {
        acc += 27;
    } else {
        acc += 5;
    }

    if ((opaque(input, 43) % 5) == 1) {
        acc += 35;
    } else {
        acc += 7;
    }

    if ((opaque(input, 44) & 4) != 0) {
        acc += 43;
    } else {
        acc += 9;
    }

    if ((opaque(input, 45) % 7) < 3) {
        acc += 47;
    } else {
        acc += 10;
    }

    if ((opaque(input, 46) & 8) != 0) {
        acc += 51;
    } else {
        acc += 11;
    }

    if ((opaque(input, 47) % 4) == 3) {
        acc += 56;
    } else {
        acc += 12;
    }

    if ((opaque(input, 48) % 6) == 4) {
        acc += 60;
    } else {
        acc += 13;
    }

    if ((opaque(input, 49) & 16) != 0) {
        acc += 66;
    } else {
        acc += 14;
    }

    // ---------------------------------------------------------------
    // Loop cluster 4
    // ---------------------------------------------------------------
    int l4 = opaque(input, 400);

    for (int m = 0; m < 60; ++m) {
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

        if ((opaque(l4 + m, 406) & 8) != 0) {
            l4 += 34;
        } else {
            l4 += 10;
        }

        if ((opaque(l4 + m, 407) % 7) == 3) {
            l4 += 20;
        } else {
            l4 += 5;
        }

        if ((opaque(l4 + m, 408) % 9) < 4) {
            l4 += 23;
        } else {
            l4 += 6;
        }
    }

    // ---------------------------------------------------------------
    // Acyclic bridge 4
    // ---------------------------------------------------------------
    if ((opaque(input, 50) & 1) == 0) {
        acc += 16;
    } else {
        acc += 2;
    }

    if ((opaque(input, 51) % 3) == 1) {
        acc += 22;
    } else {
        acc += 3;
    }

    if ((opaque(input, 52) % 5) < 2) {
        acc += 28;
    } else {
        acc += 4;
    }

    if ((opaque(input, 53) & 4) != 0) {
        acc += 34;
    } else {
        acc += 5;
    }

    if ((opaque(input, 54) % 7) == 0) {
        acc += 40;
    } else {
        acc += 6;
    }

    if ((opaque(input, 55) & 8) != 0) {
        acc += 46;
    } else {
        acc += 7;
    }

    if ((opaque(input, 56) % 4) == 2) {
        acc += 52;
    } else {
        acc += 8;
    }

    if ((opaque(input, 57) % 6) == 1) {
        acc += 58;
    } else {
        acc += 9;
    }

    if ((opaque(input, 58) & 16) != 0) {
        acc += 64;
    } else {
        acc += 10;
    }

    if ((opaque(input, 59) % 8) < 4) {
        acc += 70;
    } else {
        acc += 11;
    }

    // ---------------------------------------------------------------
    // Loop cluster 5
    // ---------------------------------------------------------------
    int l5 = opaque(input, 500);

    for (int n = 0; n < 56; ++n) {
        if ((opaque(l5 + n, 501) & 1) == 0) {
            l5 += 10;
        } else {
            l5 += 4;
        }

        if ((opaque(l5 + n, 502) % 3) == 2) {
            l5 += 18;
        } else {
            l5 += 5;
        }

        if ((opaque(l5 + n, 503) % 5) == 1) {
            l5 += 30;
        } else {
            l5 += 7;
        }

        if ((opaque(l5 + n, 504) & 2) != 0) {
            l5 += 12;
        } else {
            l5 += 3;
        }

        if ((opaque(l5 + n, 505) % 7) < 3) {
            l5 += 25;
        } else {
            l5 += 8;
        }

        if ((opaque(l5 + n, 506) & 8) != 0) {
            l5 += 33;
        } else {
            l5 += 9;
        }

        if ((opaque(l5 + n, 507) % 4) == 1) {
            l5 += 19;
        } else {
            l5 += 6;
        }

        if ((opaque(l5 + n, 508) % 11) == 5) {
            l5 += 27;
        } else {
            l5 += 10;
        }
    }

    // ---------------------------------------------------------------
    // Acyclic bridge 5
    // ---------------------------------------------------------------
    if ((opaque(input, 60) & 1) == 0) {
        acc += 17;
    } else {
        acc += 1;
    }

    if ((opaque(input, 61) % 3) == 0) {
        acc += 23;
    } else {
        acc += 2;
    }

    if ((opaque(input, 62) % 5) < 2) {
        acc += 29;
    } else {
        acc += 3;
    }

    if ((opaque(input, 63) & 4) != 0) {
        acc += 35;
    } else {
        acc += 4;
    }

    if ((opaque(input, 64) % 7) == 0) {
        acc += 41;
    } else {
        acc += 5;
    }

    if ((opaque(input, 65) & 8) != 0) {
        acc += 47;
    } else {
        acc += 6;
    }

    if ((opaque(input, 66) % 4) == 1) {
        acc += 53;
    } else {
        acc += 7;
    }

    if ((opaque(input, 67) % 6) < 3) {
        acc += 59;
    } else {
        acc += 8;
    }

    if ((opaque(input, 68) & 16) != 0) {
        acc += 65;
    } else {
        acc += 9;
    }

    if ((opaque(input, 69) % 8) == 2) {
        acc += 71;
    } else {
        acc += 10;
    }

    // ---------------------------------------------------------------
    // Loop cluster 6
    // ---------------------------------------------------------------
    int l6 = opaque(input, 600);

    for (int p = 0; p < 52; ++p) {
        if ((opaque(l6 + p, 601) % 4) == 0) {
            l6 += 14;
        } else {
            l6 += 5;
        }

        if ((opaque(l6 + p, 602) & 2) != 0) {
            l6 += 21;
        } else {
            l6 += 7;
        }

        if ((opaque(l6 + p, 603) % 6) == 5) {
            l6 += 28;
        } else {
            l6 += 8;
        }

        if ((opaque(l6 + p, 604) & 16) != 0) {
            l6 += 13;
        } else {
            l6 += 4;
        }

        if ((opaque(l6 + p, 605) % 5) < 2) {
            l6 += 24;
        } else {
            l6 += 6;
        }

        if ((opaque(l6 + p, 606) & 8) != 0) {
            l6 += 31;
        } else {
            l6 += 9;
        }

        if ((opaque(l6 + p, 607) % 7) == 3) {
            l6 += 17;
        } else {
            l6 += 5;
        }

        if ((opaque(l6 + p, 608) % 9) < 4) {
            l6 += 22;
        } else {
            l6 += 7;
        }

        if ((opaque(l6 + p, 609) % 11) == 6) {
            l6 += 26;
        } else {
            l6 += 8;
        }
    }

    // ---------------------------------------------------------------
    // Final merge
    // Only additive combination of the local loop states.
    // This preserves decomposability and should strongly favor
    // clustered ILPs over one large monolithic ILP.
    // ---------------------------------------------------------------
    acc += (l1 & 255);
    acc += (l2 & 255);
    acc += (l3 & 255);
    acc += (l4 & 255);
    acc += (l5 & 255);
    acc += (l6 & 255);

    // ---------------------------------------------------------------
    // Large acyclic suffix
    // Again based only on input to avoid re-coupling the clusters.
    // ---------------------------------------------------------------
    if ((opaque(input, 70) & 1) == 0) {
        acc += 12;
    } else {
        acc += 1;
    }

    if ((opaque(input, 71) & 2) == 0) {
        acc += 18;
    } else {
        acc += 2;
    }

    if ((opaque(input, 72) % 3) == 2) {
        acc += 24;
    } else {
        acc += 3;
    }

    if ((opaque(input, 73) % 5) == 1) {
        acc += 30;
    } else {
        acc += 4;
    }

    if ((opaque(input, 74) & 4) != 0) {
        acc += 36;
    } else {
        acc += 5;
    }

    if ((opaque(input, 75) % 7) < 3) {
        acc += 42;
    } else {
        acc += 6;
    }

    if ((opaque(input, 76) & 8) != 0) {
        acc += 48;
    } else {
        acc += 7;
    }

    if ((opaque(input, 77) % 4) == 0) {
        acc += 54;
    } else {
        acc += 8;
    }

    if ((opaque(input, 78) % 6) == 5) {
        acc += 60;
    } else {
        acc += 9;
    }

    if ((opaque(input, 79) & 16) != 0) {
        acc += 66;
    } else {
        acc += 10;
    }

    if ((opaque(input, 80) % 9) < 4) {
        acc += 72;
    } else {
        acc += 11;
    }

    if ((opaque(input, 81) & 32) != 0) {
        acc += 78;
    } else {
        acc += 12;
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