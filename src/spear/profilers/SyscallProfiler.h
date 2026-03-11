/*
 * Copyright (c) 2026 Maximilian Krebs
 * All rights reserved.
*/

#ifndef SRC_SPEAR_PROFILERS_SYSCALLPROFILER_H_
#define SRC_SPEAR_PROFILERS_SYSCALLPROFILER_H_

#include "Profiler.h"

/**
 * Component to gather system specific information based on the profiler architecture
 */
class SyscallProfiler : public Profiler {
 public:
    /**
     * Generic constructor without purpose
     */
    explicit SyscallProfiler() : Profiler("SYSCALL") {}

    /**
     * Gather information about syscalls and return them as JSON object
     * @return
     */
    json profile() override;
};

#endif  // SRC_SPEAR_PROFILERS_SYSCALLPROFILER_H_
