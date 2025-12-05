
#ifndef SPEAR_DRAMPROFILER_H
#define SPEAR_DRAMPROFILER_H
#include "Profiler.h"

class DRAMProfiler : public Profiler {
public:
    DRAMProfiler(int iterations, const std::string &codePath): Profiler(iterations) {
        std::vector<std::string> filenames;
        std::string base = "/dram/compiled/";

        for (const auto& entry : std::filesystem::directory_iterator(codePath + base)) {
            if (entry.is_regular_file()) {
                std::string filename = entry.path().filename().string();
                filenames.push_back(entry.path().filename().string()); // only the file name, not full path
            }
        }

        for (const std::string& filename : filenames) {
            _profileCode[filename] = codePath + base + filename;
        }
    }
    json profile() override;

private:
    /**
     * Mapping of instruction names to profile program paths
     */
    std::map<std::string, std::string> _profileCode;

    /**
     * Measure a given file for its energy usage using the amounts of repetitions specific in the object
     * @param file Path the file is stored at
     * @return Returns vector containing all recorded measurement values
     */
    [[nodiscard]] std::vector<double> _measureFile(const std::string& file) const;

    /**
     * Calculates a moving average on the given data with the specified window
     * @param data Raw data the average will be calculated on
     * @param windowSize Size of the window
     * @return Vector containing the moving averages of the raw data
     */
    std::vector<double> _movingAverage(const std::vector<double>& data, int windowSize);

    double huberMean(const std::vector<double>& data, double delta = 1.0, int maxIterations = 50, double tolerance = 1e-6);

    double standard_deviation(const std::vector<double>& v);
};

#endif //SPEAR_DRAMPROFILER_H