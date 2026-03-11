/*
 * Copyright (c) 2026 Maximilian Krebs
 * All rights reserved.
*/

#ifndef SRC_SPEAR_PROFILERS_SYSCALLPROFILER_H_
#define SRC_SPEAR_PROFILERS_SYSCALLPROFILER_H_

#include "Profiler.h"
#include "syscall_trace.skel.h"


// Custom deleters so we don't forget cleanup
struct RingBufDeleter {
    void operator()(ring_buffer* rb) const { ring_buffer__free(rb); }
};
struct SkelDeleter {
    void operator()(syscall_trace_bpf* skel) const { syscall_trace_bpf__destroy(skel); }
};

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
