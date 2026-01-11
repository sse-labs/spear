/*
 * Copyright (c) 2026 Maximilian Krebs
 * All rights reserved.
*/

#include "profilers/CPUProfiler.h"

#include <sys/wait.h>
#include <sys/mman.h>
#include <vector>
#include <limits>
#include <cstdio>
#include <string>
#include <map>
#include <iostream>

#include "RegisterReader.h"

#define PROGRAMITERATIONS 100000


json CPUProfiler::profile() {
    std::map<std::string, std::vector<double>> measurements;
    std::map<std::string, double> results;

    for (const auto& [key, value] : _profileCode) {
        std::vector<double> measuredEnergy = this->_measureFile(value);
        measurements[key] = measuredEnergy;
    }

    /*double sum = 0;
    for (const auto& [key, value] : measurements) {
        // std::vector<double> filtered = _movingAverage(value, this->iterations/100);
        double mean = std::accumulate(value.begin(), value.end(), 0.0) / (double) value.size();
        results[key] = mean;

        if (key != "_cachewarmer" && key != "_noise") {
            sum += mean;
        }
    }*/

    std::vector<double> flatMeasurements;
    for (const auto& [key, value] : results) {
        flatMeasurements.push_back(value);
    }

    /*double epsilon = 1e-6;

    // Find the minimum value
    double min_val = *std::min_element(flatMeasurements.begin(), flatMeasurements.end());

    // Find the median
    std::nth_element(flatMeasurements.begin(),
                     flatMeasurements.begin() + flatMeasurements.size() / 2,
                     flatMeasurements.end());
    double median_val = flatMeasurements[flatMeasurements.size() / 2];

    // Clip the median so it does not exceed min value
    double common_error = std::min(median_val, min_val - epsilon);

    double mean_over_all_entries = sum / results.size();
    double min = std::numeric_limits<double>::max();

    for (const auto& [key, value] : results)
        min = std::min(min, value);

    for (const auto& [key, value] : results) {
        results[key] = value - common_error;
    }*/

    for (const auto& [key, value] : measurements) {
        double sd = standard_deviation(value);
        results[key] = huberMean(value, 1.345 * sd, 100, 6.103515625e-05);
    }

    double noiseval = results["_noise"];
    for (const auto& [key, value] : measurements) {
        results[key] = results[key]/PROGRAMITERATIONS;
    }

    return results;
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
    const int NUM_CORES = 12;

    std::vector<double> results;
    results.reserve(this->iterations * NUM_CORES);

    #ifdef __linux__
    RegisterReader powReader(0);

    // Shared memory for initial energy values of each child
    double* sharedEnergyBefore = reinterpret_cast<double*>(mmap(nullptr,
                                                NUM_CORES * sizeof(double),
                                                PROT_READ | PROT_WRITE,
                                                MAP_SHARED | MAP_ANONYMOUS,
                                                -1, 0));

    char* args[] = { const_cast<char*>(file.c_str()), nullptr };

    uint64_t iters = (runtime != -1) ? runtime : this->iterations;

    // Pin parent to a dedicated core (optional)
    cpu_set_t parentMask;
    CPU_ZERO(&parentMask);
    CPU_SET(1, &parentMask);
    if (sched_setaffinity(0, sizeof(parentMask), &parentMask) == -1) {
        perror("sched_setaffinity (parent)");
        exit(1);
    }

    for (uint64_t it = 0; it < iters; /* manual increment inside */) {
        pid_t pids[NUM_CORES];
        bool validIteration = true;    // assume good; flip to false on invalid diff

        // ----------------------------------------------------
        // 1. Launch 12 processes: one on each core
        // ----------------------------------------------------
        for (int core = 0; core < NUM_CORES; core++) {
            pid_t pid = fork();

            if (pid == 0) {
                // -------- Child code --------
                cpu_set_t mask;
                CPU_ZERO(&mask);
                CPU_SET(core, &mask);

                if (sched_setaffinity(0, sizeof(mask), &mask) == -1) {
                    perror("sched_setaffinity (child)");
                    exit(1);
                }

                // Record initial energy
                sharedEnergyBefore[core] = powReader.getEnergy();

                // Execute the target program
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

        // Temporary buffer for this iteration
        double iterationResults[NUM_CORES];

        // ----------------------------------------------------
        // 2. Parent waits for 12 children and verifies energy diffs
        // ----------------------------------------------------
        for (int core = 0; core < NUM_CORES; core++) {
            waitpid(pids[core], nullptr, 0);

            double after = powReader.getEnergy();
            double before = sharedEnergyBefore[core];
            double diff = after - before;

            if (diff <= 0) {
                validIteration = false;   // mark iteration invalid
            }

            iterationResults[core] = diff / NUM_CORES;
        }

        // ----------------------------------------------------
        // 3. Check if iteration was valid
        // ----------------------------------------------------
        if (!validIteration) {
            // Discard and repeat this same iteration index
            continue;
        }

        // Otherwise, commit results and increment iteration counter
        for (int core = 0; core < NUM_CORES; core++) {
            results.push_back(iterationResults[core]);
        }

        it++;   // manually increment because we used continue above
    }
    #endif

    return results;
}


double CPUProfiler::huberMean(const std::vector<double>& data, double delta, int maxIterations, double tolerance) {
    if (data.empty())
        return std::numeric_limits<double>::quiet_NaN();

    // --- Initial estimate: ordinary mean ---
    double mu = std::accumulate(data.begin(), data.end(), 0.0) / data.size();

    for (int iter = 0; iter < maxIterations; ++iter) {
        double numerator = 0.0;
        double denominator = 0.0;

        for (double x : data) {
            double r = x - mu;  // residual

            double w;
            double abs_r = std::fabs(r);

            if (abs_r <= delta)
                w = 1.0;   // full weight
            else
                w = delta / abs_r;  // down-weight outliers

            numerator   += w * x;
            denominator += w;
        }

        double newMu = numerator / denominator;

        // check convergence
        if (std::fabs(newMu - mu) < tolerance)
            return newMu;

        mu = newMu;  // update estimate
    }

    return mu;  // return after maxIterations if not converged
}


double CPUProfiler::standard_deviation(const std::vector<double>& v) {
    if (v.size() < 2) return 0.0;

    double mean = std::accumulate(v.begin(), v.end(), 0.0) / v.size();

    double sq_sum = std::accumulate(
        v.begin(), v.end(), 0.0,
        [mean](double acc, double x) {
            return acc + (x - mean) * (x - mean);
        });

    return std::sqrt(sq_sum / (v.size() - 1));  // sample stdev
}
