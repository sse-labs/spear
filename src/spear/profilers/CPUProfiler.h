

#ifndef SPEAR_CPUPROFILER_H
#define SPEAR_CPUPROFILER_H

#include "Profiler.h"

class CPUProfiler : public Profiler<double> {
public:
    CPUProfiler(const int iterations, const std::string &codePath) : Profiler(iterations) {
        std::vector<std::string> filenames;
        for (const auto& entry : std::filesystem::directory_iterator(codePath + "/")) {
            if (entry.is_regular_file()) {
                std::string filename = entry.path().filename().string();
                filenames.push_back(entry.path().filename().string()); // only the file name, not full path
            }
        }

        for (const std::string& filename : filenames) {
            _profileCode[filename] = codePath + "/" + filename;
        }
    }

    std::map<std::string, double> profile() override;

private:
    std::map<std::string, std::string> _profileCode;

    std::vector<double> _measureFile(const std::string& file) const;

    std::vector<double> _movingAverage(const std::vector<double>& data, int windowSize);
};


#endif //SPEAR_CPUPROFILER_H