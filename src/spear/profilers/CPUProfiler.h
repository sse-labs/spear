/*
 * Copyright (c) 2026 Maximilian Krebs
 * All rights reserved.
*/

#ifndef SRC_SPEAR_PROFILERS_CPUPROFILER_H_
#define SRC_SPEAR_PROFILERS_CPUPROFILER_H_

#include <map>
#include <string>
#include <vector>
#include <fstream>
#include <thread>
#include <utility>

#include "Profiler.h"
using json = nlohmann::json;

/**
 * Helper class to deal with performance mode of the CPU. It provides functions to enable and disable performance mode,
 * which is used to ensure that the CPU runs at maximum performance during profiling,
 * and to restore the default power settings after profiling is done.
 */
class CPUPowerManager {
 public:
    /**
     * Enable the performance mode
     * @return true if successful, false otherwise
     */
    static bool enablePerformanceMode();

    /**
     * Disable the performance mode
     * @return true if successful, false otherwise
     */
    static bool disablePerformanceMode();

 private:
    /**
     * Write a given value to a given file
     * @param path File to write to
     * @param value Value to write
     * @return true if successfull, false otherwise
     */
    static bool writeToFile(const std::string& path, const std::string& value);
};

/**
 * RAII Guard to ensure that the CPU performance mode is enabled during profiling and disabled afterwards, even if
 * an error occurs. The constructor enables the performance mode and the destructor disables it,
 * ensuring that the CPU settings are properly restored after profiling is done.
 */
class CPUPowerGuard {
 public:
    CPUPowerGuard() {
        enabled = CPUPowerManager::enablePerformanceMode();
    }

    ~CPUPowerGuard() {
        if (enabled) {
            CPUPowerManager::disablePerformanceMode();
        }
    }

 private:
    bool enabled = false;
};

/**
 * Component to profile the systems CPU using the profiler architecture
 */
class CPUProfiler : public Profiler {
 public:
    /**
      * Creates a new Profiler object using the given iterations.
      * Additionally, parses the given profile code directory to creat the mapping
      * @param codePath Path the profile programs are stored
      */
     explicit CPUProfiler(const std::string &codePath) : Profiler("CPU") {
        this->log("Executing CPU profiler ");
        this->log("Programs for profiling stored at " + codePath);

        std::string programs_path = codePath + "/cpu/compiled/";
        std::string meta_path = codePath + "/cpu/meta.json";

        std::vector<std::string> filenames;
        for (const auto& entry : std::filesystem::directory_iterator(programs_path)) {
            if (entry.is_regular_file()) {
                std::string filename = entry.path().filename().string();
                filenames.push_back(entry.path().filename().string());  // only the file name, not full path
            }
        }

        for (const std::string& filename : filenames) {
            _profileCode[filename] = programs_path + filename;
        }

        json metadata;
        std::ifstream fileStream(meta_path);
        if (!fileStream) {
            throw std::runtime_error("CPU profiler: Metadata file not found."
                                     "Rerun the generator and make sure the path is writable!");
        }

        metadata = json::parse(fileStream);

        if (!metadata.contains("repeated_executions")) {
            throw std::runtime_error("CPU profiler: Metadata file malformed!");
        }

        this->programiterations = metadata["repeated_executions"];

        this->log(std::string("repeated_executions ") + std::to_string(this->programiterations));

        this->number_of_cores = std::thread::hardware_concurrency();
        this->log(std::string("number of cores ") + std::to_string(this->number_of_cores));
    }

     /**
      * Calculates the median of the given vector
      * @param v vector of values to calculate the median on
      * @return median of the given vector
      */
    double _median(std::vector<double> v);

    /**
     * Calculates the mean of the given vector
     * @param v vector of values to calculate the mean on
     * @return mean of the given vector
     */
    double _mean(std::vector<double> v);

    /**
     * Calculates a regression for each instruction based on the results of the measurements.
     * Each point in the regression corresponds to a measured execution of the underlying instruction test program
     *
     * y = m * x + b
     * - y is the measured energy consumption of the program
     * - x is the number of iterations of the instruction in the program (our k value)
     * - m is the slope, which represents the per-instruction energy estimate (we need to divide this later on through
     * the number of iterations to get the per-instruction energy estimate)
     * - b is the intercept, which represents the base energy consumption of the program without any iterations
     * of the instruction
     *
     *
     * See "An Introduction to Statistical Learning" by Gareth James, Daniela Witten, Trevor Hastie and Robert
     * Tibshirani for more information about linear
     * regression and the formulas used in this function.
     *
     * @param results Vector containing mappings between instruction name and measured energy for each execution of
     * each profile program
     * @return Mapping between instruction name and pair(slope, intercept) representing the regression coefficients
     * for each instruction
     */
    std::map<std::string, std::pair<double, double>> _regression(
        const std::vector<std::map<std::string, double>>& results, const std::vector<int>& ks);

    /**
     * Profiles the system and gathers information about the energy usage of the CPU components
     * @return Returns profile as JSON object
     */
    json profile() override;

 private:
    /**
     * How many times each instruction is repeated inside each profile program.
     */
    uint64_t programiterations;

    /**
     * Number of cores available on the system.
    */
    unsigned int number_of_cores;

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
};

#endif  // SRC_SPEAR_PROFILERS_CPUPROFILER_H_
