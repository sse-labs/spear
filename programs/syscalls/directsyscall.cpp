/*
 * Copyright (c) 2026 Maximilian Krebs
 * All rights reserved.
 */

#include <iostream>
#include <sys/syscall.h>
#include <unistd.h>

int main() {
    char buffer[100];
    long bytesRead = syscall(SYS_read, 0, buffer, sizeof(buffer));

    std::cout << buffer << std::endl;
    std::cout << bytesRead << std::endl;

    return 0;
}
