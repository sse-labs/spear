/*
 * Copyright (c) 2026 Maximilian Krebs
 * All rights reserved.
 */

#include <iostream>
#include "../helper/randomFiller.cpp"

int main() {
    int length = 9000;
    int *searchroom = new int[length];
    fillArrayRandom(searchroom, length, length * 4);

    // force a sentinel to trigger one exit
    searchroom[123] = 0;

    long sum = 0;

    // MALFORMED / NON-UNIFORM LOOP
    // - multiple exits
    // - multiple exiting blocks
    // - irregular control flow
    for (int i = 0; i < length; i = i + 1) {

        // exit #1
        if (searchroom[i] == 0) {
            break;
        }

        sum += searchroom[i];

        // exit #2 (second exiting block)
        if (sum > 100000) {
            break;
        }
    }

    std::cout << "Sum of array (malformed loop): " << sum << "\n";

    delete[] searchroom;
    return 0;
}