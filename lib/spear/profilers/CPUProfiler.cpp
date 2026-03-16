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
CPUProfiler::_regression(std::vector<std::map<std::string, double>> results) {
    std::map<std::string, std::vector<std::pair<double, double>>> series;

    double MIN_PROG_ENERGY = ConfigParser::getProfilingConfiguration().min_program_energy;
    double MIN_INST_ENERGY = ConfigParser::getProfilingConfiguration().min_instruction_energy;

    /**
     * Calculates a regression for each instruction based on the results of the measurements.
     * Each point in the regression corresponds to a measured execution of the underlying instruction test program
     *
     * y = m * x + b
     * - y is the measured energy consumption of the program
     * - x is the number of iterations of the instruction in the program (our k value)
     * - m is the slope, which represents the per-instruction energy estimate (we need to divide this later on through the number of iterations to get the per-instruction energy estimate)
     * - b is the intercept, which represents the base energy consumption of the program without any iterations of the instruction
     *
     *
     * See "An Introduction to Statistical Learning" by Gareth James, Daniela Witten, Trevor Hastie and Robert
     * Tibshirani for more information about linear
     * regression and the formulas used in this function.
     */

    // Create the mapping k => energy for each instruction
    for (size_t runIdx = 0; runIdx < results.size(); ++runIdx) {
        const double x = static_cast<double>(runIdx + 1);

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
        ys.reserve(points.size());
        for (const auto& [x, y] : points) {
            xs.push_back(x);
            ys.push_back(y);
        }

        // find a median of the y values to mitigate potential outliers
        const double robustLevel = std::max(_median(ys), MIN_PROG_ENERGY);

        if (points.size() == 1) {
            // Default the intercept to the robust level and the slope to 0, since we cannot calculate a slope
            // with only one point. This means that we assume that all energy consumption is due to the base
            // energy of the program, and the instruction itself does not contribute to the energy consumption.
            regressions[instr] = {0.0, robustLevel};
            continue;
        }

        const double n = static_cast<double>(points.size());

        double x_bar = _mean(xs);
        double y_bar = _mean(ys);

        /**
         * We calculate the slope using the formula:
         *
         * \beta_1 = covariance/variance  -> slope
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

        if (std::abs(variance) > 0.0) {
            slope = covariance / variance;
            intercept = y_bar - slope * x_bar;
        }

        // Do not allow negative or zero intercepts, as they are not physically meaningful in this context
        intercept = std::max(intercept, MIN_PROG_ENERGY);
        slope = std::max(slope, MIN_INST_ENERGY);

        if (slope <= MIN_INST_ENERGY) {
            slope = MIN_INST_ENERGY;
        } else {
            // We need to divide the slope by the number of iterations to get the per-instruction energy estimate,
            // since each point corresponds to a program with a different number of iterations of the same instruction.
            slope = slope / this->programiterations;
        }

        regressions[instr] = {slope, intercept};
    }

    return regressions;
}

#include <fstream>

void CPUProfiler::_writeCSV(
    int k,
    const std::map<std::string, double>& results) const {

    std::string filename = "profiling_k_" + std::to_string(k) + ".csv";
    std::ofstream file(filename);

    if (!file) {
        throw std::runtime_error("Failed to open " + filename);
    }

    file << "instruction,median\n";
    file << std::setprecision(17);

    for (const auto& [instr, median] : results) {
        file << instr << "," << median << "\n";
    }
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

    auto finishTime = std::chrono::system_clock::now() + std::chrono::milliseconds(estimatedTotalMs);
    std::time_t finishTimeT = std::chrono::system_clock::to_time_t(finishTime);

    this->log("Estimated finish time: " + std::string(std::ctime(&finishTimeT)));

    // Warmup
    const int warmupIterations = 300;
    for (const auto& [key, value] : _profileCode) {
        std::vector<double> measuredEnergy = this->_measureFile(value, warmupIterations);
    }

    for (int i = 0; i < ks.size(); i++) {
        int iterations = ks[i];

        std::map<std::string, double> results;
        std::map<std::string, std::vector<double>> measurements = std::map<std::string, std::vector<double>>();

        for (const auto& [key, value] : _profileCode) {
            std::vector<double> measuredEnergy = this->_measureFile(value, iterations);
            measurements[key] = measuredEnergy;
        }

        for (const auto& [key, value] : _profileCode) {
            double median = huberMean(measurements[key]);
            results[key] = median;
        }

        _writeCSV(iterations, results);


        allResults.push_back(results);
    }

    // Calculate regression parameters for each instruction based on the measurements
    auto regressions = _regression(allResults);


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

    this->log("CPU profiling finished!");
    return profmapping;
}

void CPUProfiler::pinToCore(int core) {
    cpu_set_t mask;
    CPU_ZERO(&mask);
    CPU_SET(core, &mask);

    if (sched_setaffinity(0, sizeof(mask), &mask) == -1) {
        perror("sched_setaffinity");
        _exit(1);
    }
}

void CPUProfiler::workerLoop(int core, int cmd_read_fd, int done_write_fd, const std::string& file) {
    pinToCore(core);

    for (;;) {
        uint8_t cmd;
        ssize_t n = read(cmd_read_fd, &cmd, sizeof(cmd));
        if (n != sizeof(cmd)) {
            _exit(0);
        }

        if (cmd == 0) {   // stop
            _exit(0);
        }

        if (cmd == 1) {   // run
            pid_t child = fork();
            if (child == 0) {
                char* const args[] = { const_cast<char*>(file.c_str()), nullptr };
                execv(file.c_str(), args);
                perror("execv");
                _exit(127);
            }

            int status = 0;
            if (child < 0 || waitpid(child, &status, 0) == -1) {
                status = -1;
            }

            if (write(done_write_fd, &status, sizeof(status)) != sizeof(status)) {
                _exit(1);
            }
        }
    }
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

std::vector<Worker> CPUProfiler::createWorkers(const std::string& file, int measurementCore, int numberOfCores) const {
    std::vector<Worker> workers;

    for (int core = 0; core < numberOfCores; ++core) {
        if (core == measurementCore) {
            continue;
        }

        int cmd_pipe[2];
        int done_pipe[2];

        if (pipe(cmd_pipe) == -1 || pipe(done_pipe) == -1) {
            perror("pipe");
            exit(1);
        }

        pid_t pid = fork();
        if (pid == 0) {
            close(cmd_pipe[1]);
            close(done_pipe[0]);
            workerLoop(core, cmd_pipe[0], done_pipe[1], file);
            _exit(0);
        }

        if (pid < 0) {
            perror("fork");
            exit(1);
        }

        close(cmd_pipe[0]);
        close(done_pipe[1]);

        workers.push_back(Worker{
            .core = core,
            .pid = pid,
            .cmd_write_fd = cmd_pipe[1],
            .done_read_fd = done_pipe[0]
        });
    }

    return workers;
}

std::vector<double> CPUProfiler::_measureFile(const std::string& file, uint64_t runtime) const {
    std::vector<double> results;

#ifdef __linux__
    constexpr int measurementCore = 0;
    const int workerCount = number_of_cores - 1;

    if (workerCount <= 0) {
        return results;
    }

    results.reserve(runtime * workerCount);

    pinToCore(measurementCore);

    RegisterReader powReader(0);
    auto workers = createWorkers(file, measurementCore, number_of_cores);

    for (uint64_t it = 0; it < runtime; ++it) {
        uint8_t startCmd = 1;

        // Tell all workers to run
        for (auto& w : workers) {
            if (write(w.cmd_write_fd, &startCmd, sizeof(startCmd)) != sizeof(startCmd)) {
                perror("write startCmd");
                exit(1);
            }
        }

        // One global energy measurement window
        double before = powReader.getEnergy();

        bool validIteration = true;

        // Wait until all workers finished
        for (auto& w : workers) {
            int status = 0;
            if (read(w.done_read_fd, &status, sizeof(status)) != sizeof(status)) {
                perror("read done");
                exit(1);
            }

            if (status == -1 || !WIFEXITED(status) || WEXITSTATUS(status) != 0) {
                validIteration = false;
            }
        }

        double after = powReader.getEnergy();
        double diff = after - before;

        if (diff <= 0) {
            validIteration = false;
        }

        if (!validIteration) {
            --it;
            continue;
        }

        double perCore = diff / workerCount;
        for (int i = 0; i < workerCount; ++i) {
            results.push_back(perCore);
        }
    }

    // Stop workers
    uint8_t stopCmd = 0;
    for (auto& w : workers) {
        (void)write(w.cmd_write_fd, &stopCmd, sizeof(stopCmd));
        close(w.cmd_write_fd);
        close(w.done_read_fd);
        waitpid(w.pid, nullptr, 0);
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
