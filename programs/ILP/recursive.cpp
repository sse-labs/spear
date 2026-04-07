/*
 * Copyright (c) 2026 Maximilian Krebs
 * All rights reserved.
 */

#include <iostream>

void functionA() {
    static int remainingCalls = 3; // Limits recursion depth

    // Base case
    if (remainingCalls <= 0) {
        return;
    }

    --remainingCalls;

    // Recursive call
    functionA();
}

int main() {
    functionA();

    std::cout << "Program finished." << std::endl;
    return 0;
}