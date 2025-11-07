
#ifndef BA_PROFILER_H
#define BA_PROFILER_H

#include "string"
#include "vector"
#include "nlohmann/json.hpp"

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
    Profiler(int iterations) : iterations(iterations) {}

    virtual JSON

};


#endif //BA_PROFILER_H
