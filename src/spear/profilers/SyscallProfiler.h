

#ifndef SPEAR_SYSCALLPROFILER_H
#define SPEAR_SYSCALLPROFILER_H
#include "Profiler.h"

/**
 * Component to gather system specific information based on the profiler architecture
 */
class SyscallProfiler : public Profiler {
public:
    /**
     * Generic constructor without purpose
     * @param iterations Repeated measurement iterations
     */
    explicit SyscallProfiler(const int iterations) : Profiler(iterations, "SYSCALL") {}

    /**
     * Gather information about syscalls and return them as JSON object
     * @return
     */
    json profile() override;
};

#endif //SPEAR_SYSCALLPROFILER_H