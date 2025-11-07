
#include <mutex>
#include "Profiler.h"
#include <unistd.h>
#include <fcntl.h>
#include <sys/mman.h>
#include <sys/wait.h>
#include <cassert>
#include <ctime>
#include <iostream>

#include "chrono"
#include "JSONHandler.h"


Profiler::Profiler(int repetitions, std::map<std::string, std::string> *profileCode){
    this->repetitions = repetitions;
    this->profileCode = profileCode;
}

std::map<std::string, double> Profiler::profile() {

    auto codemap = *this->profileCode;
    std::map<std::string, std::vector<double>> measurements;
    std::map<std::string, double> results;

    for (const auto& [key, value] : codemap) {
        std::vector<double> measuredEnergy = measureFile(value);
        measurements[key] = measuredEnergy;
    }


    double sum = 0;
    for (const auto& [key, value] : measurements) {
        std::vector<double> filtered = movingAverage(value, this->repetitions/100);
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

    /*results["call"] = measureFile(codemap.at("call"));
    results["memory"] = measureFile(codemap.at("memory"));
    results["programflow"] = measureFile(codemap.at("programflow"));
    results["division"] = measureFile(codemap.at("division"));
    results["others"] = measureFile(codemap.at("others"));*/

    return results;
}

std::vector<double> Profiler::measureFile(const std::string& file) const {
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

    for (int i = 0; i < this->repetitions; i++){
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

    /*if(this->repetitions > 0){
        energy = accumulatedEnergy / (double) this->repetitions;
    }else{
        energy = accumulatedEnergy;
    }*/

    return results;
}

double Profiler::measureProgram(const std::string& file, long repetitions) {
    double energy = 0.0;
    auto powReader = new RegisterReader(0);
    auto *sharedEnergyBefore  = (double *) mmap(nullptr, sizeof (int) , PROT_READ | PROT_WRITE, MAP_SHARED | MAP_ANONYMOUS, -1, 0);

    double accumulatedEnergy = 0.0;
    cpu_set_t cpuMask;

    for (int i = 0; i < repetitions; i++){

        pid_t childProcessId = fork();

        if(childProcessId == 0){

            // open /dev/null for writing
            int fd = open("/dev/null", O_WRONLY);

            dup2(fd, 1);
            dup2(fd, 2);
            close(fd);

            //Clear the cache....
            const size_t bigger_than_cachesize = 20 * 1024 * 1024;

            std::vector<double> clearCacheArray(bigger_than_cachesize);
            for (size_t j = 0; j < bigger_than_cachesize; ++j){
                clearCacheArray[j] = rand();
            }

            *sharedEnergyBefore = powReader->getEnergy();

            if(execv(file.c_str(), new char*) == -1){
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

                    const size_t bigger_than_cachesize = 16 * 1024 * 1024;

                    std::vector<double> clearCacheArray(bigger_than_cachesize);
                    for (size_t j = 0; j < bigger_than_cachesize; ++j){
                        clearCacheArray[j] = rand();
                    }

                    *sharedEnergyBefore = powReader->getEnergy();

                    if(execv(file.c_str(), new char*) == -1){
                        throw std::invalid_argument("Profilecode not found!!!");
                        assert(false);
                        exit(1);
                        break;
                    }

                }else {

                    wait(nullptr);

                    energyAfter = powReader->getEnergy();
                }

                accumulatedEnergy += energyAfter - *sharedEnergyBefore;
            }else{
                accumulatedEnergy += energyAfter - *sharedEnergyBefore;
            }

        }


    }

    if(repetitions > 0){
        energy = accumulatedEnergy / (double) repetitions;
    }else{
        energy = accumulatedEnergy;
    }

    return energy;
}

double Profiler::timeProgram(const std::string& file, long repetitions) {
    double time = 0.0;
    auto powReader = new RegisterReader(0);
    auto *sharedTimeBefore  = (std::chrono::time_point<std::chrono::system_clock, std::chrono::duration<long, std::ratio<1, 1000000000>>> *) mmap(nullptr, sizeof (std::chrono::time_point<std::chrono::system_clock, std::chrono::duration<long, std::ratio<1, 1000000000>>>) , PROT_READ | PROT_WRITE, MAP_SHARED | MAP_ANONYMOUS, -1, 0);

    double accumulatedTime = 0.0;
    cpu_set_t cpuMask;

    for (int i = 0; i < repetitions; i++){

        pid_t childProcessId = fork();

        if(childProcessId == 0){

            // open /dev/null for writing
            int fd = open("/dev/null", O_WRONLY);

            dup2(fd, 1);
            dup2(fd, 2);
            close(fd);

            //Clear the cache....
            const size_t bigger_than_cachesize = 20 * 1024 * 1024;

            std::vector<double> clearCacheArray(bigger_than_cachesize);
            for (size_t j = 0; j < bigger_than_cachesize; ++j){
                clearCacheArray[j] = rand();
            }

            *sharedTimeBefore = std::chrono::high_resolution_clock::now();

            if(execv(file.c_str(), new char*) == -1){
                throw std::invalid_argument("Profilecode not found!!!");
                assert(false);
                exit(1);
                break;
            }

        }else{

            //waitpid(childProcessId, nullptr, 0);
            wait(nullptr);

            auto timeAfter = std::chrono::high_resolution_clock::now();

            /* Getting number of milliseconds as a double. */
            std::chrono::duration<double, std::milli> ms_double = timeAfter - *sharedTimeBefore;
            accumulatedTime += ms_double.count();
        }


    }

    if(repetitions > 0){
        time = accumulatedTime / (double) repetitions;
    }else{
        time = accumulatedTime;
    }

    return time/1000;
}

std::string Profiler::getCPUName() {
    char buffer[128];
    std::string result;
    std::string command = "cat /proc/cpuinfo | grep 'model name' | uniq";

    FILE* pipe = popen(command.c_str(), "r");
    if (!pipe) {
        return "failed to execute the command";
    }

    while (!feof(pipe)) {
        if (fgets(buffer, 128, pipe) != nullptr)
            result += buffer;
    }

    pclose(pipe);

    std::stringstream resstream;
    std::string segment;
    resstream << result;

    std::vector<std::string> seglist;
    while(std::getline(resstream, segment, ':'))
    {
        seglist.push_back(segment);
    }

    if(segment.length() >= 2 ){
        auto lastchar = segment[segment.length()-1];
        auto firstchar = segment[0];

        if(lastchar == '\n'){
            segment.erase(segment.length()-1);
        }

        if(firstchar == ' '){
            segment.erase(0, 1);
        }

    }

    return  segment;
}

std::string Profiler::getArchitecture() {
    char buffer[128];
    std::string result;
    std::string command = "cat /proc/cpuinfo | grep 'cpu family' | uniq";

    FILE* pipe = popen(command.c_str(), "r");
    if (!pipe) {
        return "failed to execute the command";
    }

    while (!feof(pipe)) {
        if (fgets(buffer, 128, pipe) != nullptr)
            result += buffer;
    }

    pclose(pipe);

    std::stringstream resstream;
    std::string segment;
    resstream << result;

    std::vector<std::string> seglist;
    while(std::getline(resstream, segment, ':'))
    {
        seglist.push_back(segment);
    }

    if(segment.length() >=2 ){
        auto lastchar = segment[segment.length()-1];
        auto firstchar = segment[0];

        if(lastchar == '\n'){
            segment.erase(segment.length()-1);
        }

        if(firstchar == ' '){
            segment.erase(0, 1);
        }

    }


    return segment;
}

std::string Profiler::getNumberOfCores() {
    char buffer[128];
    std::string result;
    std::string command = " cat /proc/cpuinfo | grep 'siblings' | uniq";

    FILE* pipe = popen(command.c_str(), "r");
    if (!pipe) {
        return "failed to execute the command";
    }

    while (!feof(pipe)) {
        if (fgets(buffer, 128, pipe) != nullptr)
            result += buffer;
    }

    pclose(pipe);

    std::stringstream resstream;
    std::string segment;
    resstream << result;

    std::vector<std::string> seglist;
    while(std::getline(resstream, segment, ':'))
    {
        seglist.push_back(segment);
    }

    if(segment.length() >=2 ){
        auto lastchar = segment[segment.length()-1];
        auto firstchar = segment[0];

        if(lastchar == '\n'){
            segment.erase(segment.length()-1);
        }

        if(firstchar == ' '){
            segment.erase(0, 1);
        }

    }

    return segment;
}

std::string Profiler::getUnit() {
    auto powReader = new RegisterReader(0);
    auto unit = powReader->readMultiplier();

    return std::to_string(unit);
}

std::vector<double> Profiler::movingAverage(const std::vector<double>& data, int windowSize) {
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