#ifndef SPEAR_METAPROFILER_H
#define SPEAR_METAPROFILER_H

#include "profilers/Profiler.h"

class MetaProfiler : public Profiler<std::string> {
public:
    MetaProfiler() : Profiler() {

    }

    std::map<std::string, std::string> profile() override;

private:
    std::string _getCPUName();

    std::string _getArchitecture();

    std::string _getNumberOfCores();

    std::string _getRaplUnit();

};
#endif //SPEAR_METAPROFILER_H
