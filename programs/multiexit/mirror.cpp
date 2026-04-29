/*
* Copyright (c) 2026 Maximilian Krebs
 * All rights reserved.
 */

#include <iostream>
#include <vector>

// Simulate expensive work
void expensiveWork() {
    volatile long long sum = 0;
    for (int i = 0; i < 1000000; ++i) {
        sum += i;
    }
}

// Simulate cheap work
void cheapWork() {
    volatile int sum = 0;
    for (int i = 0; i < 1000; ++i) {
        sum += i;
    }
}

int main() {
    int x = 0;
    int result = 0;

    while (true) {
        // Some loop body work
        x++;

        // Exit A: early break
        if (x == 10) {
            result = 1;
            break;
        }

        // Exit B: alternative exit
        if (x == 20) {
            result = 2;
            break;
        }
    }

    // Different work depending on which exit was taken
    if (result == 1) {
        std::cout << "Took exit A (early)\n";
        expensiveWork();   // expensive continuation
    } else if (result == 2) {
        std::cout << "Took exit B (late)\n";
        cheapWork();       // cheap continuation
    }

    return 0;
}