/*
 * Copyright (c) 2026 Maximilian Krebs
 * All rights reserved.
*/

#include "profilers/SyscallProfiler.h"
#include <bpf/libbpf.h>
#include <bpf/bpf.h>

json SyscallProfiler::profile() {
    this->log("Executing Syscallprofiler");
    json syscalls;

    syscalls["HELLO"] = "WORLD";

    // Define bpf skeleton and parameters
    libbpf_set_strict_mode(LIBBPF_STRICT_ALL);
    std::unique_ptr<syscall_trace_bpf, SkelDeleter> skel(syscall_trace_bpf__open());
    if (!skel) {
        throw std::runtime_error("failed to open syscall trace bpf");
    }

    if (syscall_trace_bpf__load(skel.get())) {
        throw std::runtime_error("failed to load syscall trace bpf");
    }

    return syscalls;
}

