#include <iostream>

void performExpensiveComputation() {
    volatile long long result = 0;

    for (long long index = 0; index < 500000000; ++index) {
        result += index % 7;
    }

    std::cout << "Expensive computation finished: " << result << std::endl;
}

int main() {
    const int loopUpperBound = 10;
    int accumulatedValue = 0;

    for (int loopIndex = 0; loopIndex < loopUpperBound; ++loopIndex) {
        accumulatedValue += loopIndex;

        if (loopIndex == 3) {
            break;
        }
    }

    // This is the common post-loop block.
    // In LLVM this should correspond to something like for.end.
    performExpensiveComputation();

    std::cout << "Accumulated value: " << accumulatedValue << std::endl;

    return 0;
}