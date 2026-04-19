/*
 * Copyright (c) 2026 Maximilian Krebs
 * All rights reserved.
*/

#include "profilers/CPUProfiler.h"

#include <sys/wait.h>
#include <sys/mman.h>
#include <vector>
#include <cstdio>
#include <string>
#include <map>
#include <iostream>
#include <utility>
#include <algorithm>

#include "CPU_vendor.h"
#include "ConfigParser.h"
#include "Logger.h"
#include "RegisterReader.h"

#define RETRYFACTOR 1.25

bool CPUPowerManager::writeToFile(const std::string& path, const std::string& value) {
    std::ofstream file(path);

    if (!file.is_open()) {
        Logger::getInstance().log("Failed to open file: " + path, LOGLEVEL::WARNING);
        return false;
    }

    file << value;

    if (!file.good()) {
        Logger::getInstance().log("Failed to write value '" + value + "' to " + path, LOGLEVEL::WARNING);
        return false;
    }

    return true;
}

bool CPUPowerManager::enablePerformanceMode() {
    Logger::getInstance().log("Enabling CPU performance mode (performance governor, turbo off)", LOGLEVEL::INFO);

    bool success = true;

    const std::string cpuBasePath = "/sys/devices/system/cpu/";

    for (const auto& entry : std::filesystem::directory_iterator(cpuBasePath)) {
        const std::string cpuPath = entry.path().string();

        if (cpuPath.find("cpu") == std::string::npos) {
            continue;
        }

        const std::string governorPath = cpuPath + "/cpufreq/scaling_governor";

        if (std::filesystem::exists(governorPath)) {
            if (!writeToFile(governorPath, "performance")) {
                success = false;
            }
        }
    }

    // Get the vendor at runtime
    int vendor = cpu_vendor_runtime();

    // Check if we are on an intel CPU
    if (vendor == CPU_VENDOR_INTEL) {
        const std::string turboPath = "/sys/devices/system/cpu/intel_pstate/no_turbo";

        if (std::filesystem::exists(turboPath)) {
            if (!writeToFile(turboPath, "1")) {
                success = false;
            }
        } else {
            Logger::getInstance().log("Intel turbo control not available (intel_pstate missing?)", LOGLEVEL::WARNING);
        }
    } else if (vendor == CPU_VENDOR_AMD) {
        // Deal with AMD CPUs
        bool foundBoostControl = false;

        for (const auto& entry : std::filesystem::directory_iterator(cpuBasePath)) {
            const std::string cpuPath = entry.path().string();

            if (cpuPath.find("cpu") == std::string::npos) {
                continue;
            }

            const std::string boostPath = cpuPath + "/cpufreq/boost";

            if (std::filesystem::exists(boostPath)) {
                foundBoostControl = true;

                if (!writeToFile(boostPath, "0")) {
                    success = false;
                }
            }
        }

        if (!foundBoostControl) {
            Logger::getInstance().log("AMD boost control not available", LOGLEVEL::WARNING);
        }
    } else {
        Logger::getInstance().log("Unknown CPU vendor, skipping turbo / boost configuration", LOGLEVEL::WARNING);
    }

    if (!success) {
        Logger::getInstance().log("Performance mode activation incomplete (missing permissions?)", LOGLEVEL::WARNING);
    }

    return success;
}

bool CPUPowerManager::disablePerformanceMode() {
    Logger::getInstance().log("Restoring CPU default mode (powersave governor, turbo on)", LOGLEVEL::INFO);

    bool success = true;
    const std::string cpuBasePath = "/sys/devices/system/cpu/";

    for (const auto& entry : std::filesystem::directory_iterator(cpuBasePath)) {
        const std::string cpuPath = entry.path().string();

        if (cpuPath.find("cpu") == std::string::npos) {
            continue;
        }

        const std::string governorPath = cpuPath + "/cpufreq/scaling_governor";

        if (std::filesystem::exists(governorPath)) {
            if (!writeToFile(governorPath, "powersave")) {
                success = false;
            }
        }
    }

    // Get the cpu vendor at runtime
    int vendor = cpu_vendor_runtime();

    // Check if we are on an intel CPU
    if (vendor == CPU_VENDOR_INTEL) {
        const std::string turboPath = "/sys/devices/system/cpu/intel_pstate/no_turbo";

        if (std::filesystem::exists(turboPath)) {
            if (!writeToFile(turboPath, "0")) {
                success = false;
            }
        } else {
            Logger::getInstance().log("Intel turbo control not available (intel_pstate missing?)", LOGLEVEL::WARNING);
        }
    } else if (vendor == CPU_VENDOR_AMD) {
        // Disable the performance mode for AMD CPUs by re-enabling boost
        bool foundBoostControl = false;

        for (const auto& entry : std::filesystem::directory_iterator(cpuBasePath)) {
            const std::string cpuPath = entry.path().string();

            if (cpuPath.find("cpu") == std::string::npos) {
                continue;
            }

            const std::string boostPath = cpuPath + "/cpufreq/boost";

            if (std::filesystem::exists(boostPath)) {
                foundBoostControl = true;

                if (!writeToFile(boostPath, "1")) {
                    success = false;
                }
            }
        }

        if (!foundBoostControl) {
            Logger::getInstance().log("AMD boost control not available", LOGLEVEL::WARNING);
        }
    } else {
        Logger::getInstance().log("Unknown CPU vendor, skipping turbo / boost restoration", LOGLEVEL::WARNING);
    }

    if (!success) {
        Logger::getInstance().log("Restoring CPU mode incomplete", LOGLEVEL::WARNING);
    }

    return success;
}

double CPUProfiler::_median(std::vector<double> v) {
    std::sort(v.begin(), v.end());

    size_t n = v.size();

    if (n % 2 == 0) {
        return (v[n/2 - 1] + v[n/2]) / 2.0;
    } else {
        return v[n/2];
    }
}

double CPUProfiler::_mean(std::vector<double> v) {
    double sum = 0.0;
    for (double value : v) {
        sum += value;
    }
    return sum / static_cast<double>(v.size());
}


std::map<std::string, std::pair<double, double>>
CPUProfiler::_regression(const std::vector<std::map<std::string, double>>& results, const std::vector<int>& ks) {
    std::map<std::string, std::vector<std::pair<double, double>>> series;

    double MIN_PROG_ENERGY = ConfigParser::getProfilingConfiguration().min_program_energy;
    double MIN_INST_ENERGY = ConfigParser::getProfilingConfiguration().min_instruction_energy;

    /**
     * Calculates a regression for each instruction based on the results of the measurements.
     * Each point in the regression corresponds to a measured execution of the underlying instruction test program
     *
     * y = m * x + b
     * - y is the measured energy consumption of the program
     * - x is the number of repetitions of the instruction test program (our k value)
     * - m is the slope, which represents the energy increase per k step
     * - b is the intercept, which represents the base energy consumption of the program without any iterations of the instruction
     *
     * See "An Introduction to Statistical Learning" by Gareth James, Daniela Witten, Trevor Hastie and Robert
     * Tibshirani for more information about linear
     * regression and the formulas used in this function.
     */

    if (results.size() != ks.size()) {
        throw std::runtime_error("CPUProfiler::_regression received mismatching results and ks sizes");
    }

    // Create the mapping k => energy for each instruction
    for (size_t runIdx = 0; runIdx < results.size(); ++runIdx) {
        const double x = static_cast<double>(ks[runIdx]);

        for (const auto& [instr, value] : results[runIdx]) {
            series[instr].push_back({x, value});
        }
    }

    std::map<std::string, std::pair<double, double>> regressions;

    for (const auto& [instr, points] : series) {
        if (points.empty()) {
            // Default the regression to the minimum values defined by the user
            regressions[instr] = {MIN_INST_ENERGY, MIN_PROG_ENERGY};
            continue;
        }

        std::vector<double> xs;
        std::vector<double> ys;
        xs.reserve(points.size());
        ys.reserve(points.size());

        for (const auto& [x, y] : points) {
            xs.push_back(x);
            ys.push_back(y);
        }

        // Find a median of the y values to mitigate potential outliers
        const double robustLevel = std::max(_median(ys), MIN_PROG_ENERGY);

        if (points.size() == 1) {
            // Default the intercept to the robust level and the slope to 0, since we cannot calculate a slope
            // with only one point. This means that we assume that all energy consumption is due to the base
            // energy of the program, and the instruction itself does not contribute to the energy consumption.
            regressions[instr] = {0.0, robustLevel};
            continue;
        }

        double x_bar = _mean(xs);
        double y_bar = _mean(ys);

        /**
         * We calculate the slope using the formula:
         *
         * \beta_1 = covariance / variance  -> slope
         * \beta_0 = y_bar - \beta_1 * x_bar  -> intercept
         *
         */

        double covariance = 0.0;
        double variance = 0.0;

        for (const auto& [x, y] : points) {
            covariance += (x - x_bar) * (y - y_bar);
            variance += (x - x_bar) * (x - x_bar);
        }

        double slope = 0.0;
        double intercept = robustLevel;

        if (variance > 0.0) {
            slope = covariance / variance;
            intercept = y_bar - slope * x_bar;
        }

        // Do not allow negative or zero intercepts, as they are not physically meaningful in this context
        intercept = std::max(intercept, MIN_PROG_ENERGY);
        slope = std::max(slope, MIN_INST_ENERGY);

        if (slope <= MIN_INST_ENERGY) {
            slope = MIN_INST_ENERGY;
        } else {
            // We need to divide the slope by the number of iterations in the benchmark body
            // to get the per-instruction energy estimate.
            slope = slope / this->programiterations;
        }

        regressions[instr] = {slope, intercept};
    }

    return regressions;
}

json CPUProfiler::profile() {
    this->log("Starting CPU profiling. This may take a while. Grab a coffee!");
    CPUPowerGuard guard;


    auto cpuregression = ConfigParser::getProfilingConfiguration().cpuregression;

    this->log("Profile programs contain " + std::to_string(this->programiterations) +
        " iterations of the underlying instruction.");

    std::vector<int> ks = cpuregression;

    std::string regressionValues;
    for (auto k : ks) {
        regressionValues += std::to_string(k) + " ";
    }
    this->log("Profiling will be performed for the following k values: " + regressionValues);


    json profmapping = json::object();
    std::vector<std::map<std::string, double>> allResults;

    // Perform a single measurement for each instruction to estimate runtime.
    auto estimateStart = std::chrono::steady_clock::now();

    for (const auto& [key, value] : _profileCode) {
        [[maybe_unused]] std::vector<double> measuredEnergy = this->_measureFile(value, 5);
    }

    auto estimateEnd = std::chrono::steady_clock::now();
    auto measuredMs = std::chrono::duration_cast<std::chrono::milliseconds>(estimateEnd - estimateStart).count();

    // The estimate run used k = 5 for each profiled file.
    constexpr double baselineK = 5.0;
    const double perKMs = static_cast<double>(measuredMs) / baselineK;

    int64_t estimatedTotalMs = 0;
    for (int k : ks) {
        estimatedTotalMs += static_cast<int64_t>(perKMs * k);
    }

    // Calculate the estimated amount of ms via the retryfactor and assume a timely overhead
    const auto estimatedSeconds = (estimatedTotalMs / 1000.0) * RETRYFACTOR;

    this->log("Profiling will take approximately " + std::to_string(estimatedSeconds) + " seconds.");

    auto finishTime = std::chrono::system_clock::now() +
                      std::chrono::milliseconds(estimatedTotalMs);
    std::time_t finishTimeT = std::chrono::system_clock::to_time_t(finishTime);

    this->log("Estimated finish time: " + std::string(std::ctime(&finishTimeT)));

    for (int i = 0; i < ks.size(); i++) {
        int iterations = ks[i];

        std::map<std::string, double> results;
        std::map<std::string, std::vector<double>> measurements = std::map<std::string, std::vector<double>>();

        for (const auto& [key, value] : _profileCode) {
            std::vector<double> measuredEnergy = this->_measureFile(value, iterations);
            measurements[key] = measuredEnergy;
        }

        for (const auto& [key, value] : _profileCode) {
            double median = _median(measurements[key]);
            results[key] = median;
        }

        allResults.push_back(results);
    }

    // Calculate regression parameters for each instruction based on the measurements
    auto regressions = _regression(allResults, ks);


    // Collect the slope and intercept for each instruction and store them in the final profile mapping.
    // We also calculate a constant offset for the program energy based on the intercepts of all instructions,
    // as they all include the base energy of the program. We use the median of the intercepts to mitigate
    // potential outliers.
    std::vector<double> intercepts;
    for (const auto& [instr, coeff] : regressions) {
        profmapping[instr] = coeff.first;
        intercepts.push_back(coeff.second);
    }

    // Store the constant offset in the profile mapping under a special key.
    // This offset represents the base energy consumption of the program
    double constanteOffset = _median(intercepts);
    profmapping["_programoffset"] = constanteOffset;
    profmapping["_unknown_cost"] = ConfigParser::getProfilingConfiguration().min_instruction_energy;

    this->log("CPU profiling finished!");
    return profmapping;
}


std::vector<double> CPUProfiler::_movingAverage(const std::vector<double>& data, int windowSize) {
    std::vector<double> result;

    if (windowSize <= 0 || data.size() < windowSize) {
        std::cerr << "Invalid window size. " << data.size() << std::endl;
        return result;
    }

    double sum = std::accumulate(data.begin(), data.begin() + windowSize, 0.0);
    result.push_back(sum / windowSize);

    for (size_t i = windowSize; i < data.size(); ++i) {
        sum += data[i] - data[i - windowSize];
        result.push_back(sum / windowSize);
    }

    return result;
}

std::vector<double> CPUProfiler::_measureFile(const std::string& file, uint64_t runtime) const {
    std::vector<double> results;
    results.reserve(runtime * number_of_cores);

    #ifdef __linux__
    RegisterReader powReader(0);

    // Shared memory for initial energy values of each child
    double* sharedEnergyBefore = reinterpret_cast<double*>(mmap(nullptr,
                                                number_of_cores * sizeof(double),
                                                PROT_READ | PROT_WRITE,
                                                MAP_SHARED | MAP_ANONYMOUS,
                                                -1, 0));

    char* args[] = { const_cast<char*>(file.c_str()), nullptr };

    uint64_t iters = runtime;

    // Pin parent to a dedicated core
    cpu_set_t parentMask;
    CPU_ZERO(&parentMask);
    CPU_SET(1, &parentMask);
    if (sched_setaffinity(0, sizeof(parentMask), &parentMask) == -1) {
        perror("sched_setaffinity (parent)");
        exit(1);
    }

    for (uint64_t it = 0; it < iters; /* manual increment inside */) {
        std::vector<pid_t> pids(number_of_cores);
        bool validIteration = true;  // assume good; flip to false on invalid diff

        // Launch processes: one on each core
        for (int core = 0; core < number_of_cores; core++) {
            pid_t pid = fork();

            if (pid == 0) {
                cpu_set_t mask;
                CPU_ZERO(&mask);
                CPU_SET(core, &mask);

                if (sched_setaffinity(0, sizeof(mask), &mask) == -1) {
                    perror("sched_setaffinity (child)");
                    exit(1);
                }

                sharedEnergyBefore[core] = powReader.getEnergy();

                if (execv(file.c_str(), args) == -1) {
                    perror("execv");
                    exit(1);
                }
            } else if (pid > 0) {
                pids[core] = pid;
            } else {
                perror("fork");
                exit(1);
            }
        }

        std::vector<double> iterationResults(number_of_cores);

        for (int core = 0; core < number_of_cores; core++) {
            waitpid(pids[core], nullptr, 0);

            double after = powReader.getEnergy();
            double before = sharedEnergyBefore[core];
            double diff = after - before;

            if (diff <= 0) {
                validIteration = false;
            }

            iterationResults[core] = diff / number_of_cores;
        }

        if (!validIteration) {
            continue;
        }

        for (int core = 0; core < number_of_cores; core++) {
            results.push_back(iterationResults[core]);
        }

        it++;
    }

    #endif

    return results;
}

