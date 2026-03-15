/*
* Copyright (c) 2026 Maximilian Krebs
 * All rights reserved.
 */

#include <iostream>
#include <ostream>
#include <sys/uio.h>
#include <unistd.h>

int main() {
    char buffer1[50] = {};
    char buffer2[50] = {};

    iovec iov[2];
    iov[0].iov_base = buffer1;
    iov[0].iov_len = sizeof(buffer1) - 1;  // leave space for null terminator
    iov[1].iov_base = buffer2;
    iov[1].iov_len = sizeof(buffer2) - 1;  // leave space for null terminator

    ssize_t bytesRead = readv(STDIN_FILENO, iov, 2);

    std::cout << buffer1 << buffer2 << std::endl;
    std::cout << bytesRead << std::endl;

    return 0;
}