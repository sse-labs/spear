
/*
 * Copyright (c) 2026 Maximilian Krebs
 * All rights reserved.
 */

#ifndef SPEAR_CONFIGURATIONOBJECTS_H
#define SPEAR_CONFIGURATIONOBJECTS_H

#include <map>
#include <string>

#include "valuespace.h"

struct LegacyAnalysisConfiguration {
    Mode mode;
    Format format;
    Strategy strategy;
    bool deepcalls;
};

struct CPURegressionConfig {
    int limit;
    int step;
    int offset;
};

struct SyscallProfilingConfig {
    int runtime;
    double defaultEnergy;
    int maxSyscallId;
};

/**
 * Holds profiling-related configuration options parsed from the config file.
 */
struct ProfilingConfiguration {
    double min_program_energy;
    double min_instruction_energy;
    CPURegressionConfig cpuregression;
    SyscallProfilingConfig syscallconfig;
};

/**
 * Holds analysis-related configuration options parsed from the config file.
 */
struct AnalysisConfiguration {
    AnalysisType analysisType;
    bool cachingEnabled;
    bool feasibilityEnabled;
    LegacyAnalysisConfiguration legacyconfig;
    std::map<std::string, int64_t> fallback;
};

#endif //SPEAR_CONFIGURATIONOBJECTS_H
