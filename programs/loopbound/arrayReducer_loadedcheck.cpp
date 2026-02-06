/*
 * Copyright (c) 2026 Maximilian Krebs
 * All rights reserved.
 */

#include <iostream>
#include <array>
#include <algorithm>
#include "../helper/randomFiller.cpp"

// lengthPtr is a function argument (pointer)
// The loop bound is a LOAD, not a constant literal
long sumArray(int *array, int *lengthPtr) {
    long s = 0;

    // Dominating store makes the load constant
    *lengthPtr = 9000;

    for (int i = 0; i < *lengthPtr; i = i + 1) {
        int a1 = array[i];
        s += a1;
    }

    return s;
}

int main() {
    int length = 0;
    int *searchroom = new int[9000];
    fillArrayRandom(searchroom, 9000, 9000 * 4);

    long sum = sumArray(searchroom, &length);

    std::cout << "Sum of array: " << sum << "\n";

    delete[] searchroom;
    return 0;
}