
#ifndef SPEAR_DRAMPROFILER_H
#define SPEAR_DRAMPROFILER_H
#include "Profiler.h"

class DRAMProfiler : public Profiler {
public:
    DRAMProfiler(int iterations): Profiler(iterations) {}
    std::map<std::string, double> profile() override;
};

#endif //SPEAR_DRAMPROFILER_H