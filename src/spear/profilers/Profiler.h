
#ifndef BA_PROFILER_H
#define BA_PROFILER_H

#include "string"
#include "vector"
#include "nlohmann/json.hpp"
#include <map>

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


#endif //BA_PROFILER_H
