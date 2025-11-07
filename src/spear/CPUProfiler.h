

#ifndef SPEAR_CPUPROFILER_H
#define SPEAR_CPUPROFILER_H
#include "Profiler.h"

class CPUProfiler : public Profiler {
public:
    CPUProfiler(int iterations) : Profiler(iterations) {}

    std::map<std::string, double> profile() override;
};


#endif //SPEAR_CPUPROFILER_H