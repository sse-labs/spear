#include "profilers/CPUProfiler.h"

#include <iostream>
#include <sys/wait.h>
#include <sys/mman.h>

#include "RegisterReader.h"

std::map<std::string, double> CPUProfiler::profile() {
    std::map<std::string, std::vector<double>> measurements;
    std::map<std::string, double> results;

    for (const auto& [key, value] : _profileCode) {
        std::vector<double> measuredEnergy = this->_measureFile(value);
        measurements[key] = measuredEnergy;
    }

    double sum = 0;
    for (const auto& [key, value] : measurements) {
        std::vector<double> filtered = _movingAverage(value, this->iterations/100);
        double mean = std::accumulate(filtered.begin(), filtered.end(), 0.0) / filtered.size();
        results[key] = mean;

        if (key != "_cachewarmer") {
            sum += mean;
        }
    }

    std::vector<double> flatMeasurements;
    for (const auto& [key, value] : results) {
        flatMeasurements.push_back(value);
    }

    double epsilon = 1e-6;

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

std::vector<double> CPUProfiler::_measureFile(const std::string& file) const {
    std::vector<double> results = {};
    double energy = 0.0;
    auto powReader = new RegisterReader(0);
    auto *sharedEnergyBefore  = (double *) mmap(nullptr, sizeof (int) , PROT_READ | PROT_WRITE, MAP_SHARED | MAP_ANONYMOUS, -1, 0);
    char* args[] = { const_cast<char*>(file.c_str()), nullptr };

    double accumulatedEnergy = 0.0;
    cpu_set_t cpuMask;

    cpu_set_t set;
    CPU_ZERO(&set);
    CPU_SET(0, &set);

    if (sched_setaffinity(0, sizeof(set), &set) == -1) {
        perror("sched_setaffinity");
        exit(1);
    }

    for (int i = 0; i < this->iterations; i++){
        double diff = 0;
        pid_t childProcessId = fork();

        if(childProcessId == 0){

            *sharedEnergyBefore = powReader->getEnergy();

            if(execv(file.c_str(), args) == -1){
                throw std::invalid_argument("Profilecode not found!!!");
                assert(false);
                exit(1);
                break;
            }

        }else{

            //waitpid(childProcessId, nullptr, 0);
            wait(nullptr);

            double energyAfter = powReader->getEnergy();

            //If the register overflows...
            if(*sharedEnergyBefore > energyAfter){
                pid_t ic_pid = fork();

                if(ic_pid == 0){

                    *sharedEnergyBefore = powReader->getEnergy();

                    if(execv(file.c_str(), args) == -1){
                        throw std::invalid_argument("Profilecode not found!!!");
                        assert(false);
                        exit(1);
                        break;
                    }

                }else {

                    wait(nullptr);

                    energyAfter = powReader->getEnergy();
                }

                diff = energyAfter - *sharedEnergyBefore;
            }else{
                diff = energyAfter - *sharedEnergyBefore;
            }

        }
        results.push_back(diff);
    }

    return results;
}