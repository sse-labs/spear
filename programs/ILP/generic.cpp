/*
* Copyright (c) 2026 Maximilian Krebs
 * All rights reserved.
 */

#include <iostream>
#include <ostream>

int main() {
    int accumulatedValue = 0;

    for (int loopIndex = 0; loopIndex < 500; ++loopIndex) {
        accumulatedValue += 255 * loopIndex;
    }

    std::cout << accumulatedValue << std::endl;

    return 0;
}

