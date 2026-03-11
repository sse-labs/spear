/*
 * Copyright (c) 2026 Maximilian Krebs
 * All rights reserved.
*/


#ifndef SRC_SPEAR_PROFILERS_PROFILER_H_
#define SRC_SPEAR_PROFILERS_PROFILER_H_

#include <utility>
#include <iostream>
#include <string>
#include "nlohmann/json.hpp"

using json = nlohmann::json;

/**
 * Simple class to profile the llvm-code and output the data in an appropriate format
 */
class Profiler {
 public:
    const std::string tag;

    /**
     * Constructor
     * @param iterations
     */
    explicit Profiler(std::string tag) : tag(std::move(tag)) {}

    /**
     * Constructor
     */
    explicit Profiler() {}

    /**
     * Profiles the respective component.
     * @return Returns a mapping between value => energy
     */
    virtual json profile() = 0;

    void log(std::string const & message) {
        std::cout << "[" << this->tag << "]: " << message << std::endl;
    }

    /**
     * Default descructor
     */
    virtual ~Profiler() = default;
};


#endif  // SRC_SPEAR_PROFILERS_PROFILER_H_
