/*
 * Copyright (c) 2026 Maximilian Krebs
 * All rights reserved.
 */


#include <iostream>

// Forward declarations
void functionA();
void functionB();

void functionA() {
    static int remainingCalls = 3; // Shared limit across calls

    // Base case
    if (remainingCalls <= 0) {
        return;
    }

    --remainingCalls;

    // Call B
    functionB();
}

void functionB() {
    // Call A
    functionA();
}

int main() {
    functionA();
    std::cout << "Program finished." << std::endl;
    return 0;
}