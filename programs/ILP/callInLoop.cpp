/*
 * Copyright (c) 2026 Maximilian Krebs
 * All rights reserved.
 */

#include <iostream>
#include <ostream>

int b(int inputValue) {
    // Add some complexity to further discourage inlining
    volatile int mask = 245;
    return inputValue | mask;
}

int main() {
    int accumulatedValue = 0;

    for (int loopIndex = 0; loopIndex < 500; ++loopIndex) {
        accumulatedValue += b(accumulatedValue);
    }

    std::cout << accumulatedValue << std::endl;

    return 0;
}

