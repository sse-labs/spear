
/*
 * Copyright (c) 2026 Maximilian Krebs
 * All rights reserved.
 */

#ifndef SRC_SPEAR_CONFIGURATION_CONFIGURATIONOBJECTS_H_
#define SRC_SPEAR_CONFIGURATION_CONFIGURATIONOBJECTS_H_

#include <map>
#include <string>

#include "configuration/valuespace.h"

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
    AnalysisOutputMode analysisOutputMode;
    std::string outputDirectory;
    bool cachingEnabled;
    bool feasibilityEnabled;
    bool writeDotFiles;
    LegacyAnalysisConfiguration legacyconfig;
    std::unordered_map<std::string, std::unordered_map<std::string, double>> fallback;
};

#endif  // SRC_SPEAR_CONFIGURATION_CONFIGURATIONOBJECTS_H_
