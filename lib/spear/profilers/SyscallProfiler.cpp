
#include "profilers/SyscallProfiler.h"


json SyscallProfiler::profile() {
    this->log("Executing Syscallprofiler");
    json syscalls;

    syscalls["HELLO"] = "WORLD";

    return syscalls;
}
