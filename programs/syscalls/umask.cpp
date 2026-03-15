/*
* Copyright (c) 2026 Maximilian Krebs
 * All rights reserved.
 */

#include <iostream>
#include <ostream>
#include <sys/stat.h>
#include <unistd.h>

int main() {
    mode_t oldMask = umask(022);

    std::cout << oldMask << std::endl;

    return 0;
}