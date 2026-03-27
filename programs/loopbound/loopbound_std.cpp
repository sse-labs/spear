/*
 * Copyright (c) 2026 Maximilian Krebs
 * All rights reserved.
 */

#include <iostream>
#include <ostream>

int main() {
    int q = 0;
    int j = 10;

    for (int i = 0; i < 1000; i++) {
        q += i;
        j += i*2;
    }


    std::cout << j << std::endl;
    std::cout << q << std::endl;

    return 0;
}