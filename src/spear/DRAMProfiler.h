/*
 * Copyright (c) 2026 Maximilian Krebs
 * All rights reserved.
*/

#ifndef SRC_SPEAR_DRAMPROFILER_H_
#define SRC_SPEAR_DRAMPROFILER_H_

#include <string>
#include <map>
#include "Profiler.h"

class DRAMProfiler : public Profiler {
 public:
    explicit DRAMProfiler(int iterations): Profiler(iterations) {}
    std::map<std::string, double> profile() override;
};

#endif  // SRC_SPEAR_DRAMPROFILER_H_
