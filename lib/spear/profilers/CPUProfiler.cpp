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



double CPUProfiler::_median(std::vector<double> v) {
    std::sort(v.begin(), v.end());

    size_t n = v.size();

    if (n % 2 == 0) {
        return (v[n/2 - 1] + v[n/2]) / 2.0;
    } else {
        return v[n/2];
    }
}


json CPUProfiler::profile() {
    this->log("Starting CPU profiling. This may take a while. Grab a coffee!");

    int ks[] = {10, 20, 50, 100, 200, 500, 1000};

    json allResults = json::object();

    for (int i = 0; i < sizeof(ks) / sizeof(ks[0]); i++) {
        int iterations = ks[i];

        std::map<std::string, double> results;
        std::map<std::string, std::vector<double>> measurements;

        for (const auto& [key, value] : _profileCode) {
            std::vector<double> measuredEnergy = this->_measureFile(value, iterations);
            measurements[key] = measuredEnergy;
        }

        for (const auto& [key, value] : _profileCode) {
            double median = _median(measurements[key]);
            results[key] = median;
        }

        // Save into JSON result too
        allResults[std::to_string(iterations)] = json::object();
        for (const auto& [key, median] : results) {
            allResults[std::to_string(iterations)][key] = median;
        }

        // Write one CSV file per k
        std::string filename = "profile_" + std::to_string(iterations) + ".csv";
        std::ofstream file(filename);

        if (!file.is_open()) {
            throw std::runtime_error("Failed to open file: " + filename);
        }

        file << "program,median\n";
        for (const auto& [key, median] : results) {
            file << key << "," << median << "\n";
        }

        file.close();
        this->log("Wrote " + filename);
    }

    this->log("CPU profiling finished!");
    return allResults;
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
    results.reserve(this->iterations * number_of_cores);

    #ifdef __linux__
    RegisterReader powReader(0);

    // Shared memory for initial energy values of each child
    double* sharedEnergyBefore = reinterpret_cast<double*>(mmap(nullptr,
                                                number_of_cores * sizeof(double),
                                                PROT_READ | PROT_WRITE,
                                                MAP_SHARED | MAP_ANONYMOUS,
                                                -1, 0));

    char* args[] = { const_cast<char*>(file.c_str()), nullptr };

    uint64_t iters = (runtime != -1) ? runtime : this->iterations;

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
