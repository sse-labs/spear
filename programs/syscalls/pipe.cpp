/*
* Copyright (c) 2026 Maximilian Krebs
 * All rights reserved.
 */

#include <iostream>
#include <ostream>
#include <unistd.h>

int main() {
    int pipefd[2];
    int result = pipe(pipefd);

    std::cout << pipefd[0] << std::endl;
    std::cout << pipefd[1] << std::endl;
    std::cout << result << std::endl;

    close(pipefd[0]);
    close(pipefd[1]);

    return 0;
}