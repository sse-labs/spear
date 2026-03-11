/*
 * Copyright (c) 2026 Maximilian Krebs
 * All rights reserved.
*/

#include "profilers/SyscallProfiler.h"


json SyscallProfiler::profile() {
    this->log("Executing Syscallprofiler");
    json syscalls;

    syscalls["HELLO"] = "WORLD";

    return syscalls;
}
