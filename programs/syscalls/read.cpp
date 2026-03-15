/*
 * Copyright (c) 2026 Maximilian Krebs
 * All rights reserved.
 */

#include <iostream>
#include <ostream>
#include <unistd.h>

int main() {
    char buffer[100];
    ssize_t bytesRead = read(STDIN_FILENO, buffer, sizeof(buffer) - 1 /* leave space for null terminator */);

    std::cout << buffer << std::endl;
    std::cout << bytesRead << std::endl;

    return 0;
}

