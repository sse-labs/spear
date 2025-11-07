
#ifndef BA_PROFILER_H
#define BA_PROFILER_H

#include "string"
#include "vector"
#include "nlohmann/json.hpp"
#include <map>

/**
 * Simple class to profile the llvm-code and output the data in an appropriate format
 */
template<typename T>
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
    virtual std::map<std::string, T> profile() = 0;

    /**
     * Default descructor
     */
    virtual ~Profiler() = default;
};


#endif //BA_PROFILER_H
