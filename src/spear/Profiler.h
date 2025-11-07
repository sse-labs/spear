
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

    /**
     * Profiles the respective component.
     * @return Returns a mapping between value => energy
     */
    virtual std::map<std::string, double> profile() = 0;

    /**
     * Default descructor
     */
    virtual ~Profiler() = default;
};


#endif //BA_PROFILER_H
