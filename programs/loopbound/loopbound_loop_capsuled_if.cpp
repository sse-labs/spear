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

int main() {
    int q = 0;
    int a = 10;

    for (int i = 0; i < 1000; i++) {
        for (int j = 0; j < 300; j++) {
            if (q < 0) {
                q += i*j;
            }
        }
    }

    std::cout << q << std::endl;

    return 0;
}