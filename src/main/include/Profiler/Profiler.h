
#ifndef BA_PROFILER_H
#define BA_PROFILER_H

#include "../Rapl-reader/RegisterReader.h"
#include "../Rapl-reader/PowercapReader.h"
#include "string"
#include "vector"
#include "nlohmann/json.hpp"

/**
 * Simple class to profile the llvm-code and output the data in an appropriate format
 */
class Profiler {
    public:
        /**
         * Times a single program will be executed
         */
        long repetitions;

        std::map<std::string, std::string> *profileCode;
        /**
         * Creates a Profiler object and sets the iterations and repetitions property according to the parameters
         * @constructor
         * @param it Iterations for the average
         * @param rep Times a program will be executed repeatedly
         */
        Profiler(int rep, std::map<std::string, std::string> *profileCode);

        /**
         * Runs the profile and returns the values for the benchmarked files
         */
        std::map<std::string, double> profile();

        /**
         * Get the name of the cpu used by the system
         * @return string representation of the cpu name
         */
        static std::string getCPUName();

        /**
         * Get the name of the architecture used by the cpu
         * @return string representation of the cpu architecture
         */
        static std::string getArchitecture();

        /**
         * Get the number of cpu cores
         * @return string representation of the number of cpu cores
         */
        static std::string getNumberOfCores();

        /**
         * Get the energy unit of the system
         * @return string representation of energy unit of the system
         */
        static std::string getUnit();

        /**
         * Calculate the energy used by the given program and average it
         * @param file path to the program to profile
         * @param repetitions repetitions used to average the energy used by the program
         * @return energy value of the given program
         */
        static double measureProgram(const std::string& file, long repetitions);

        /**
         * Calculate the time used by the given program
         * @param file path to the program
         * @param repetitions repetitions used to average the energy used by the program
         * @return time used by the given function as double
         */
        static double timeProgram(const std::string& file, long repetitions);
    private:
        /**
         * Benchmarks a single file, calculates the used energy and returns the calculated value
         * @param file String to the file that needs to be benchmarked
         * @return The used energy
         */
        std::vector<double> measureFile(const std::string& file) const;

        std::vector<double> movingAverage(const std::vector<double>& data, int windowSize);
};


#endif //BA_PROFILER_H
