/*
 * Copyright (c) 2026 Maximilian Krebs
 * All rights reserved.
*/

#ifndef SRC_SPEAR_PROFILERS_METAPROFILER_H_
#define SRC_SPEAR_PROFILERS_METAPROFILER_H_

#include <string>
#include "profilers/Profiler.h"

/**
 * Component to gather system specific information based on the profiler architecture
 */
class MetaProfiler : public Profiler {
 public:
    /**
     * Generic constructor without purpose
     * @param iterations Repeated measurement iterations
     */
    explicit MetaProfiler(const int iterations) : Profiler(iterations) {}

    /**
     * Gather information about the system and return them as JSON object
     * @return
     */
    json profile() override;

    /**
     * Create a start timestamp
     * @return
     */
    std::string startTime();

    /**
     * Create a stop timestamp
     * @return
     */
    std::string stopTime();

 private:
    /**
     * Parse the cpuinfo file in linux and return the CPU name
     * @return String representing the CPU name
     */
    static std::string _getCPUName();

    /**
     * Parse the cpuinfo file in linux and return the architecture
     * @return String representing the architecture name
     */
    static std::string _getArchitecture();

    /**
     * Parse the cpuinfo file in linux and return the number of cores
     * @return String representing the number of cores
     */
    static std::string _getNumberOfCores();

    /**
     * Analyse the RAPL interface and retrieve the used energy increment unit
     * @return String representing the rapl unit
     */
    static double _getRaplUnit();

    /**
     * Read the given system file
     * @return
     */
    static std::string _readSystemFile(std::string file);

    /**
     * Create current timestanmp and return it as string
     * @return String representation of the timestamp
     */
    static std::string _getTimeStr();
};
#endif  // SRC_SPEAR_PROFILERS_METAPROFILER_H_
