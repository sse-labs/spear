#include "profilers/CPUProfiler.h"

#include <iostream>
#include <sys/wait.h>
#include <sys/mman.h>
#include "profilers/SchedGatedEnergy.h"

#include "RegisterReader.h"

json CPUProfiler::profile() {
    std::map<std::string, std::vector<double>> measurements;
    std::map<std::string, double> results;

    for (const auto& [key, value] : _profileCode) {
        std::vector<double> measuredEnergy = this->_measureFile(value);
        measurements[key] = measuredEnergy;
    }

    /*double sum = 0;
    for (const auto& [key, value] : measurements) {
        //std::vector<double> filtered = _movingAverage(value, this->iterations/100);
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
        results[key] = results[key] - noiseval;
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

std::vector<double> CPUProfiler::_measureFile(const std::string& file, long runtime) const {
    std::vector<double> results;
    results.reserve((runtime != -1) ? runtime : this->iterations);

#ifdef __linux__
    long iters = (runtime != -1) ? runtime : this->iterations;

    // Pin parent to a different CPU than the child (optional but helps)
    {
        cpu_set_t pm;
        CPU_ZERO(&pm);
        CPU_SET(1, &pm);
        (void)sched_setaffinity(0, sizeof(pm), &pm);
    }

    for (long it = 0; it < iters; /*manual inc*/) {
        pid_t pid = fork();
        if (pid == -1) {
            std::fprintf(stderr, "fork failed: %s\n", std::strerror(errno));
            break;
        }

        if (pid == 0) {
            // Child: pin to a chosen CPU (e.g., CPU 0)
            cpu_set_t cm;
            CPU_ZERO(&cm);
            CPU_SET(0, &cm);
            if (sched_setaffinity(0, sizeof(cm), &cm) == -1) {
                std::perror("sched_setaffinity(child)");
                _exit(1);
            }

            char* args[] = { const_cast<char*>(file.c_str()), nullptr };
            execv(file.c_str(), args);
            std::perror("execv");
            _exit(1);
        }

        // Parent: start gated-energy tracer for this child PID
        SchedGatedEnergy gate;
        if (!gate.start(static_cast<uint32_t>(pid))) {
            // If tracer fails, kill child and abort this iteration
            kill(pid, SIGKILL);
            waitpid(pid, nullptr, 0);
            continue;
        }

        // Poll until child exits
        int status = 0;
        while (true) {
            // Poll events frequently to gate segments precisely
            gate.poll(5);

            pid_t w = waitpid(pid, &status, WNOHANG);
            if (w == -1) {
                std::fprintf(stderr, "waitpid failed: %s\n", std::strerror(errno));
                kill(pid, SIGKILL);
                waitpid(pid, nullptr, 0);
                break;
            }
            if (w == pid) break;
        }

        // Drain a little to catch last switch_out; then finalize
        for (int k = 0; k < 10; ++k) gate.poll(1);

        const double gated_energy = gate.stop_and_get_energy();

        // Basic validity check
        if (gated_energy <= 0.0) {
            // discard and repeat iteration index
            continue;
        }

        results.push_back(gated_energy);
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
                w = 1.0;  // full weight
            else
                w = delta / abs_r; // down-weight outliers

            numerator   += w * x;
            denominator += w;
        }

        double newMu = numerator / denominator;

        // check convergence
        if (std::fabs(newMu - mu) < tolerance)
            return newMu;

        mu = newMu;  // update estimate
    }

    return mu; // return after maxIterations if not converged
}


double CPUProfiler::standard_deviation(const std::vector<double>& v) {
    if (v.size() < 2) return 0.0;

    double mean = std::accumulate(v.begin(), v.end(), 0.0) / v.size();

    double sq_sum = std::accumulate(
        v.begin(), v.end(), 0.0,
        [mean](double acc, double x) {
            return acc + (x - mean) * (x - mean);
        });

    return std::sqrt(sq_sum / (v.size() - 1)); // sample stdev
}