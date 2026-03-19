/*
 * Copyright (c) 2026 Maximilian Krebs
 * All rights reserved.
 */


/*
* Copyright (c) 2026 Maximilian Krebs
 * All rights reserved.
 */

#include <iostream>
#include <ostream>

int main(int argc, char **argv) {
    int q = 0;

    if (argc == 0) {
        for (int i = 0; i < 1000; i++) {
            q += i;
        }
    } else {
        for (int i = 0; i < 90000; i++) {
            q += i;
        }
    }

    std::cout << q << std::endl;

    return 0;
}