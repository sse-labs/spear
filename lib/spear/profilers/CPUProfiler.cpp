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
#include <utility>
#include <algorithm>

#include "ConfigParser.h"
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

    std::vector<int> ks;

    auto cpuregression = ConfigParser::getProfilingConfiguration().cpuregression;

    // Create k values for regression-based analysis.
    int limit = cpuregression.limit;  // maximum k value to test
    int step = cpuregression.step;  // step size for k values
    int offset = cpuregression.offset;  // start with k=1 to have a baseline point for regression

    this->log("Profile programs contain " + std::to_string(this->programiterations) +
        " iterations of the underlying instruction.");

    this->log("Generating k values for regression: limit=" + std::to_string(limit) +
              ", step=" + std::to_string(step) +
              ", offset=" + std::to_string(offset));

    for (int k = offset; k <= limit; k += step) {
        ks.push_back(k);
    }

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

    const auto estimatedSeconds = estimatedTotalMs / 1000.0;
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
    results.reserve(runtime);

#ifdef __linux__
    RegisterReader powReader(0);

    char* args[] = { const_cast<char*>(file.c_str()), nullptr };

    uint64_t iterations = runtime;

    // Pin parent to a dedicated core
    cpu_set_t parentMask;
    CPU_ZERO(&parentMask);
    CPU_SET(1, &parentMask);
    if (sched_setaffinity(0, sizeof(parentMask), &parentMask) == -1) {
        perror("sched_setaffinity (parent)");
        exit(1);
    }

    for (uint64_t iteration = 0; iteration < iterations; ++iteration) {
        std::vector<pid_t> childProcessIds(number_of_cores, -1);
        std::vector<std::array<int, 2>> readyPipes(number_of_cores);
        std::vector<std::array<int, 2>> startPipes(number_of_cores);

        for (int core = 0; core < number_of_cores; ++core) {
            if (pipe(readyPipes[core].data()) == -1) {
                perror("pipe (ready)");
                exit(1);
            }

            if (pipe(startPipes[core].data()) == -1) {
                perror("pipe (start)");
                exit(1);
            }
        }

        // Launch one process per core
        for (int core = 0; core < number_of_cores; ++core) {
            pid_t childProcessId = fork();

            if (childProcessId == 0) {
                // Child process

                // Close unused pipe ends in the child
                close(readyPipes[core][0]);
                close(startPipes[core][1]);

                for (int otherCore = 0; otherCore < number_of_cores; ++otherCore) {
                    if (otherCore == core) {
                        continue;
                    }

                    close(readyPipes[otherCore][0]);
                    close(readyPipes[otherCore][1]);
                    close(startPipes[otherCore][0]);
                    close(startPipes[otherCore][1]);
                }

                cpu_set_t childMask;
                CPU_ZERO(&childMask);
                CPU_SET(core, &childMask);

                if (sched_setaffinity(0, sizeof(childMask), &childMask) == -1) {
                    perror("sched_setaffinity (child)");
                    exit(1);
                }

                // Signal to the parent that this child is pinned and ready
                char readySignal = 'R';
                ssize_t readyWriteResult = write(readyPipes[core][1], &readySignal, 1);
                if (readyWriteResult != 1) {
                    perror("write (ready)");
                    exit(1);
                }

                close(readyPipes[core][1]);

                // Wait for the parent to release all children at nearly the same time
                char startSignal = 0;
                ssize_t startReadResult = read(startPipes[core][0], &startSignal, 1);
                if (startReadResult != 1) {
                    perror("read (start)");
                    exit(1);
                }

                close(startPipes[core][0]);

                if (execv(file.c_str(), args) == -1) {
                    perror("execv");
                    exit(1);
                }

                exit(1);
            } else if (childProcessId > 0) {
                // Parent process
                childProcessIds[core] = childProcessId;

                // Close unused pipe ends in the parent
                close(readyPipes[core][1]);
                close(startPipes[core][0]);
            } else {
                perror("fork");
                exit(1);
            }
        }

        // Wait until all children are pinned and ready
        for (int core = 0; core < number_of_cores; ++core) {
            char readySignal = 0;
            ssize_t readyReadResult = read(readyPipes[core][0], &readySignal, 1);
            if (readyReadResult != 1) {
                perror("read (ready)");
                exit(1);
            }

            close(readyPipes[core][0]);
        }

        // Start the measurement window only after all children are ready
        double energyBefore = powReader.getEnergy();

        // Release all children as closely together as possible
        for (int core = 0; core < number_of_cores; ++core) {
            char startSignal = 'S';
            ssize_t startWriteResult = write(startPipes[core][1], &startSignal, 1);
            if (startWriteResult != 1) {
                perror("write (start)");
                exit(1);
            }

            close(startPipes[core][1]);
        }

        bool validIteration = true;

        for (int core = 0; core < number_of_cores; ++core) {
            int childStatus = 0;
            if (waitpid(childProcessIds[core], &childStatus, 0) == -1) {
                perror("waitpid");
                exit(1);
            }

            if (!WIFEXITED(childStatus) || WEXITSTATUS(childStatus) != 0) {
                validIteration = false;
            }
        }

        double energyAfter = powReader.getEnergy();
        double energyDifference = energyAfter - energyBefore;

        if (!validIteration) {
            continue;
        }

        if (energyDifference <= 0.0) {
            continue;
        }

        // Store the average batch energy per child as one sample
        results.push_back(energyDifference / static_cast<double>(number_of_cores));
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
