/*
 * Copyright (c) 2026 Maximilian Krebs
 * All rights reserved.
*/


#ifndef SRC_SPEAR_PROFILERS_PROFILER_H_
#define SRC_SPEAR_PROFILERS_PROFILER_H_

#include <map>
#include "string"
#include "vector"
#include "nlohmann/json.hpp"

using json = nlohmann::json;

/**
 * Simple class to profile the llvm-code and output the data in an appropriate format
 */
class Profiler {
 public:
    const int iterations;

    /**
     * Constructor
     * @param iterations
     */
    explicit Profiler(int iterations) : iterations(iterations) {}

    /**
     * Constructor
     */
    explicit Profiler() : iterations(0) {}

    /**
     * Profiles the respective component.
     * @return Returns a mapping between value => energy
     */
    virtual json profile() = 0;

    /**
     * Default descructor
     */
    virtual ~Profiler() = default;
};


#endif  // SRC_SPEAR_PROFILERS_PROFILER_H_
