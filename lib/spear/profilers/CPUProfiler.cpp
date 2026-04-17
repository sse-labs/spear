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

#include <numeric>
#include <cmath>
#include <stdexcept>

#include "ConfigParser.h"
#include "RegisterReader.h"

double CPUProfiler::_median(std::vector<double> values) {
    if (values.empty()) {
        return 0.0;
    }

    std::sort(values.begin(), values.end());

    const size_t numberOfValues = values.size();

    if (numberOfValues % 2 == 0) {
        return (values[numberOfValues / 2 - 1] + values[numberOfValues / 2]) / 2.0;
    }

    return values[numberOfValues / 2];
}

double CPUProfiler::_mean(const std::vector<double>& values) {
    if (values.empty()) {
        return 0.0;
    }

    double sum = 0.0;
    for (double value : values) {
        sum += value;
    }

    return sum / static_cast<double>(values.size());
}

double CPUProfiler::computeMedianAbsoluteDeviation(const std::vector<double>& values, double center) {
    if (values.empty()) {
        return 0.0;
    }

    std::vector<double> absoluteDeviations;
    absoluteDeviations.reserve(values.size());

    for (double value : values) {
        absoluteDeviations.push_back(std::fabs(value - center));
    }

    return _median(absoluteDeviations);
}

CPUProfiler::LinearRegressionParameters CPUProfiler::solveWeightedLeastSquares(
    const std::vector<double>& xValues,
    const std::vector<double>& yValues,
    const std::vector<double>& weights) {

    LinearRegressionParameters regressionParameters;

    if (xValues.size() != yValues.size() || xValues.size() != weights.size() || xValues.empty()) {
        return regressionParameters;
    }

    double weightSum = 0.0;
    double weightedXSum = 0.0;
    double weightedYSum = 0.0;

    for (size_t index = 0; index < xValues.size(); ++index) {
        weightSum += weights[index];
        weightedXSum += weights[index] * xValues[index];
        weightedYSum += weights[index] * yValues[index];
    }

    if (weightSum <= 0.0) {
        return regressionParameters;
    }

    const double xMean = weightedXSum / weightSum;
    const double yMean = weightedYSum / weightSum;

    double weightedCovariance = 0.0;
    double weightedVariance = 0.0;

    for (size_t index = 0; index < xValues.size(); ++index) {
        const double centeredX = xValues[index] - xMean;
        const double centeredY = yValues[index] - yMean;

        weightedCovariance += weights[index] * centeredX * centeredY;
        weightedVariance += weights[index] * centeredX * centeredX;
    }

    if (weightedVariance <= 0.0) {
        regressionParameters.slope = 0.0;
        regressionParameters.intercept = yMean;
        regressionParameters.valid = true;
        return regressionParameters;
    }

    regressionParameters.slope = weightedCovariance / weightedVariance;
    regressionParameters.intercept = yMean - regressionParameters.slope * xMean;
    regressionParameters.valid = true;

    return regressionParameters;
}

CPUProfiler::LinearRegressionParameters CPUProfiler::solveOrdinaryLeastSquares(
    const std::vector<double>& xValues,
    const std::vector<double>& yValues) {

    std::vector<double> unitWeights(xValues.size(), 1.0);
    return solveWeightedLeastSquares(xValues, yValues, unitWeights);
}

CPUProfiler::LinearRegressionParameters CPUProfiler::solveHuberRegression(
    const std::vector<double>& xValues,
    const std::vector<double>& yValues,
    double huberDelta,
    int maximumIterations,
    double convergenceTolerance) {

    LinearRegressionParameters regressionParameters = solveOrdinaryLeastSquares(xValues, yValues);

    if (!regressionParameters.valid || xValues.size() < 2) {
        return regressionParameters;
    }

    std::vector<double> residuals;
    residuals.reserve(xValues.size());

    std::vector<double> weights(xValues.size(), 1.0);

    for (int iteration = 0; iteration < maximumIterations; ++iteration) {
        residuals.clear();

        for (size_t index = 0; index < xValues.size(); ++index) {
            const double prediction =
                regressionParameters.slope * xValues[index] + regressionParameters.intercept;
            residuals.push_back(yValues[index] - prediction);
        }

        const double residualMedian = _median(residuals);
        double robustScale = computeMedianAbsoluteDeviation(residuals, residualMedian);

        // Convert MAD to an estimate of the standard deviation for normally distributed noise.
        robustScale *= 1.4826;

        if (robustScale < 1e-12) {
            robustScale = 1e-12;
        }

        for (size_t index = 0; index < residuals.size(); ++index) {
            const double standardizedResidual = std::fabs(residuals[index]) / robustScale;

            if (standardizedResidual <= huberDelta) {
                weights[index] = 1.0;
            } else {
                weights[index] = huberDelta / standardizedResidual;
            }
        }

        LinearRegressionParameters updatedRegressionParameters =
            solveWeightedLeastSquares(xValues, yValues, weights);

        if (!updatedRegressionParameters.valid) {
            break;
        }

        const double slopeChange =
            std::fabs(updatedRegressionParameters.slope - regressionParameters.slope);
        const double interceptChange =
            std::fabs(updatedRegressionParameters.intercept - regressionParameters.intercept);

        regressionParameters = updatedRegressionParameters;

        if (slopeChange < convergenceTolerance && interceptChange < convergenceTolerance) {
            break;
        }
    }

    return regressionParameters;
}

std::map<std::string, std::pair<double, double>>
CPUProfiler::_regression(const std::vector<std::map<std::string, double>>& results, const std::vector<int>& ks) {
    std::map<std::string, std::vector<std::pair<double, double>>> series;

    double minimumProgramEnergy = ConfigParser::getProfilingConfiguration().min_program_energy;
    double minimumInstructionEnergy = ConfigParser::getProfilingConfiguration().min_instruction_energy;

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
    for (size_t runIndex = 0; runIndex < results.size(); ++runIndex) {
        const double x = static_cast<double>(ks[runIndex]);

        for (const auto& [instruction, value] : results[runIndex]) {
            series[instruction].push_back({x, value});
        }
    }

    std::map<std::string, std::pair<double, double>> regressions;

    for (const auto& [instruction, points] : series) {
        if (points.empty()) {
            // Default the regression to the minimum values defined by the user
            regressions[instruction] = {minimumInstructionEnergy, minimumProgramEnergy};
            continue;
        }

        std::vector<double> xValues;
        std::vector<double> yValues;
        xValues.reserve(points.size());
        yValues.reserve(points.size());

        for (const auto& [x, y] : points) {
            xValues.push_back(x);
            yValues.push_back(y);
        }

        // Find a median of the y values to mitigate potential outliers
        const double robustLevel = std::max(_median(yValues), minimumProgramEnergy);

        if (points.size() == 1) {
            // Default the intercept to the robust level and the slope to 0, since we cannot calculate a slope
            // with only one point. This means that we assume that all energy consumption is due to the base
            // energy of the program, and the instruction itself does not contribute to the energy consumption.
            regressions[instruction] = {0.0, robustLevel};
            continue;
        }

        /**
         * We calculate the slope using a robust Huber regression.
         *
         * For small residuals, Huber behaves like ordinary least squares.
         * For large residuals, points are down-weighted so that outliers
         * influence the regression less strongly.
         */

        constexpr double huberDelta = 1.345;
        constexpr int maximumIterations = 50;
        constexpr double convergenceTolerance = 1e-9;

        LinearRegressionParameters regressionParameters =
            solveHuberRegression(xValues, yValues, huberDelta, maximumIterations, convergenceTolerance);

        double slope = 0.0;
        double intercept = robustLevel;

        if (regressionParameters.valid) {
            slope = regressionParameters.slope;
            intercept = regressionParameters.intercept;
        }

        // Do not allow negative or zero intercepts, as they are not physically meaningful in this context
        intercept = std::max(intercept, minimumProgramEnergy);
        slope = std::max(slope, minimumInstructionEnergy);

        if (slope <= minimumInstructionEnergy) {
            slope = minimumInstructionEnergy;
        } else {
            // We need to divide the slope by the number of iterations in the benchmark body
            // to get the per-instruction energy estimate.
            slope = slope / this->programiterations;
        }

        regressions[instruction] = {slope, intercept};
    }

    return regressions;
}

json CPUProfiler::profile() {
    this->log("Starting CPU profiling. This may take a while. Grab a coffee!");

    std::vector<int> ks;

    auto cpuRegressionConfiguration = ConfigParser::getProfilingConfiguration().cpuregression;

    // Create k values for regression-based analysis.
    int limit = cpuRegressionConfiguration.limit;  // maximum k value to test
    int step = cpuRegressionConfiguration.step;  // step size for k values
    int offset = cpuRegressionConfiguration.offset;  // start with k=1 to have a baseline point for regression

    this->log("Profile programs contain " + std::to_string(this->programiterations) +
        " iterations of the underlying instruction.");

    this->log("Generating k values for regression: limit=" + std::to_string(limit) +
              ", step=" + std::to_string(step) +
              ", offset=" + std::to_string(offset));

    for (int k = offset; k <= limit; k += step) {
        ks.push_back(k);
    }

    json profileMapping = json::object();
    std::vector<std::map<std::string, double>> allResults;

    // Perform a single measurement for each instruction to estimate runtime.
    auto estimateStart = std::chrono::steady_clock::now();

    for (const auto& [key, value] : _profileCode) {
        [[maybe_unused]] std::vector<double> measuredEnergy = this->_measureFile(value, 5);
    }

    auto estimateEnd = std::chrono::steady_clock::now();
    auto measuredMilliseconds =
        std::chrono::duration_cast<std::chrono::milliseconds>(estimateEnd - estimateStart).count();

    // The estimate run used k = 5 for each profiled file.
    constexpr double baselineK = 5.0;
    const double millisecondsPerK = static_cast<double>(measuredMilliseconds) / baselineK;

    int64_t estimatedTotalMilliseconds = 0;
    for (int k : ks) {
        estimatedTotalMilliseconds += static_cast<int64_t>(millisecondsPerK * k);
    }

    const auto estimatedSeconds = estimatedTotalMilliseconds / 1000.0;
    this->log("Profiling will take approximately " + std::to_string(estimatedSeconds) + " seconds.");

    auto finishTime = std::chrono::system_clock::now() +
                      std::chrono::milliseconds(estimatedTotalMilliseconds);
    std::time_t finishTimeValue = std::chrono::system_clock::to_time_t(finishTime);

    this->log("Estimated finish time: " + std::string(std::ctime(&finishTimeValue)));

    for (size_t index = 0; index < ks.size(); ++index) {
        int iterations = ks[index];

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

        allResults.push_back(results);
    }

    // Calculate regression parameters for each instruction based on the measurements
    auto regressions = _regression(allResults, ks);

    // Collect the slope and intercept for each instruction and store them in the final profile mapping.
    // We also calculate a constant offset for the program energy based on the intercepts of all instructions,
    // as they all include the base energy of the program. We use the median of the intercepts to mitigate
    // potential outliers.
    std::vector<double> intercepts;
    for (const auto& [instruction, coefficients] : regressions) {
        profileMapping[instruction] = coefficients.first;
        intercepts.push_back(coefficients.second);
    }

    // Store the constant offset in the profile mapping under a special key.
    // This offset represents the base energy consumption of the program
    double constantOffset = _median(intercepts);
    profileMapping["_programoffset"] = constantOffset;
    profileMapping["_unknown_cost"] = ConfigParser::getProfilingConfiguration().min_instruction_energy;

    this->log("CPU profiling finished!");
    return profileMapping;
}

std::vector<double> CPUProfiler::_movingAverage(const std::vector<double>& data, int windowSize) {
    std::vector<double> result;

    if (windowSize <= 0 || data.size() < static_cast<size_t>(windowSize)) {
        std::cerr << "Invalid window size. " << data.size() << std::endl;
        return result;
    }

    double sum = std::accumulate(data.begin(), data.begin() + windowSize, 0.0);
    result.push_back(sum / windowSize);

    for (size_t index = windowSize; index < data.size(); ++index) {
        sum += data[index] - data[index - windowSize];
        result.push_back(sum / windowSize);
    }

    return result;
}

std::vector<double> CPUProfiler::_measureFile(const std::string& file, uint64_t runtime) const {
    std::vector<double> results;
    results.reserve(runtime);

#ifdef __linux__
    RegisterReader powerReader(0);

    char* args[] = {const_cast<char*>(file.c_str()), nullptr};

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
        double energyBefore = powerReader.getEnergy();

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

        double energyAfter = powerReader.getEnergy();
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
    if (data.empty()) {
        return std::numeric_limits<double>::quiet_NaN();
    }

    // --- Initial estimate: ordinary mean ---
    double mu = std::accumulate(data.begin(), data.end(), 0.0) / static_cast<double>(data.size());

    for (int iteration = 0; iteration < maxIterations; ++iteration) {
        double numerator = 0.0;
        double denominator = 0.0;

        for (double value : data) {
            double residual = value - mu;

            double weight;
            double absoluteResidual = std::fabs(residual);

            if (absoluteResidual <= delta) {
                weight = 1.0;   // full weight
            } else {
                weight = delta / absoluteResidual;  // down-weight outliers
            }

            numerator += weight * value;
            denominator += weight;
        }

        double newMu = numerator / denominator;

        // check convergence
        if (std::fabs(newMu - mu) < tolerance) {
            return newMu;
        }

        mu = newMu;  // update estimate
    }

    return mu;  // return after maxIterations if not converged
}

double CPUProfiler::standard_deviation(const std::vector<double>& values) {
    if (values.size() < 2) {
        return 0.0;
    }

    double mean = std::accumulate(values.begin(), values.end(), 0.0) / static_cast<double>(values.size());

    double squaredSum = 0.0;
    for (double value : values) {
        double difference = value - mean;
        squaredSum += difference * difference;
    }

    return std::sqrt(squaredSum / static_cast<double>(values.size() - 1));  // sample stdev
}