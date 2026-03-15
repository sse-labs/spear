/*
 * Copyright (c) 2026 Maximilian Krebs
 * All rights reserved.
 */


#include <iostream>
int read(int filepointer, void* buffer, size_t count) {
    return 0;
}


int main() {
    int val = read(9, nullptr, 0);

    std::cout << val << std::endl;

    return 0;
}

