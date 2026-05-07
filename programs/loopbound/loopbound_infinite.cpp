/*
 * Copyright (c) 2026 Maximilian Krebs
 * All rights reserved.
 */


#include <iostream>
#include <ostream>

int main() {
    int q = 0;
    int j = 10;

    while (true) {
        q += 1;
        j += q*2;

        if (q >1000) {
            j = 0;
        }
    }


    std::cout << j << std::endl;
    std::cout << q << std::endl;

    return 0;
}