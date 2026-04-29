#include <iostream>

// Simulate an expensive computation
void performExpensiveComputation() {
    volatile long long result = 0;

    for (long long index = 0; index < 500000000; ++index) {
        result += index % 7;
    }

    std::cout << "Expensive computation finished: " << result << std::endl;
}

int main() {
    const int loopUpperBound = 10;
    bool loopCompletedNormally = true;

    for (int loopIndex = 0; loopIndex < loopUpperBound; ++loopIndex) {
        std::cout << "Loop iteration: " << loopIndex << std::endl;

        if (loopIndex == 3) {
            loopCompletedNormally = false;
            break;
        }
    }

    if (loopCompletedNormally) {
        // Expensive path only after normal loop termination
        performExpensiveComputation();
    } else {
        // Break path exists, but is cheap
        std::cout << "Loop exited via break, cheap path." << std::endl;
    }

    return 0;
}