/*
* Copyright (c) 2026 Maximilian Krebs
 * All rights reserved.
 */

#include <iostream>
#include <ostream>

int main(int argc, char **argv) {
    int q = 0;

    if (q == 2) {
        for (int i = 0; i < 1000; i++) {
            q += i;
        }
    } else {
        q = 1;
    }

    std::cout << q << std::endl;

    return 0;
}