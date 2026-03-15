/*
* Copyright (c) 2026 Maximilian Krebs
 * All rights reserved.
 */

#include <iostream>
#include <ostream>
#include <unistd.h>

int main() {
    pid_t pid = fork();

    std::cout << pid << std::endl;

    return 0;
}