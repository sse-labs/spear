/*
* Copyright (c) 2026 Maximilian Krebs
 * All rights reserved.
 */

#include <iostream>

int main(){
    int length = 9;

    if (length < 10) {
        if (length > 20) {
            std::cout << "This will never be printed\n";
        }
    }

    std::cout << "Sum of array: " << length++ << "\n";

    return 0;
}