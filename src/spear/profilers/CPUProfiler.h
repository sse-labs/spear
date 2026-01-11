/*
 * Copyright (c) 2026 Maximilian Krebs
 * All rights reserved.
*/

#ifndef SRC_SPEAR_PROFILERS_CPUPROFILER_H_
#define SRC_SPEAR_PROFILERS_CPUPROFILER_H_

#include <map>
#include <string>
#include <vector>
#include "Profiler.h"


/**
 * Component to profile the systems CPU using the profiler architecture
 */
class CPUProfiler : public Profiler {
 public:
    /**
     * Creates a new Profiler object using the given iterations.
     * Additionally, parses the given profile code directory to creat the mapping
     * @param iterations Number of times the measurement should be repeated
     * @param codePath Path the profile programs are stored
     */
    CPUProfiler(const int iterations, const std::string &codePath) : Profiler(iterations) {
        std::vector<std::string> filenames;
        for (const auto& entry : std::filesystem::directory_iterator(codePath + "/cpu/compiled/")) {
            if (entry.is_regular_file()) {
                std::string filename = entry.path().filename().string();
                filenames.push_back(entry.path().filename().string());  // only the file name, not full path
            }
        }

        for (const std::string& filename : filenames) {
            _profileCode[filename] = codePath + "/cpu/compiled/" + filename;
        }
    }

    /**
     * Profiles the system and gathers information about the energy usage of the CPU components
     * @return Returns profile as JSON object
     */
    json profile() override;

 private:
    /**
     * Mapping of instruction names to profile program paths
     */
    std::map<std::string, std::string> _profileCode;

    /**
     * Measure a given file for its energy usage using the amounts of repetitions specific in the object
     * @param file Path the file is stored at
     * @return Returns vector containing all recorded measurement values
     */
    [[nodiscard]] std::vector<double> _measureFile(const std::string& file, uint64_t runtime = -1) const;

    /**
     * Calculates a moving average on the given data with the specified window
     * @param data Raw data the average will be calculated on
     * @param windowSize Size of the window
     * @return Vector containing the moving averages of the raw data
     */
    std::vector<double> _movingAverage(const std::vector<double>& data, int windowSize);

    double huberMean(
        const std::vector<double>& data,
        double delta = 1.0,
        int maxIterations = 50,
        double tolerance = 1e-6);

    double standard_deviation(const std::vector<double>& v);

    double _measureIdle(double durationSeconds) const;
};

#endif  // SRC_SPEAR_PROFILERS_CPUPROFILER_H_
