/*
 * Copyright (c) 2026 Maximilian Krebs
 * All rights reserved.
*/

#include "profilers/MetaProfiler.h"

#include "RegisterReader.h"
#include <chrono>
#include <sstream>
#include <string>
#include <vector>
#include <cstdio>

json MetaProfiler::profile() {
    json metainformation;

    metainformation["version"] = "2.0.0";
    metainformation["name"] = _getCPUName();
    metainformation["architecture"] = _getArchitecture();
    metainformation["cores"] = _getNumberOfCores();
    metainformation["raplunit"] = _getRaplUnit();
    metainformation["iterations"] = iterations;

    return metainformation;
}

std::string MetaProfiler::startTime() {
    return _getTimeStr();
}

std::string MetaProfiler::stopTime() {
    return _getTimeStr();
}

std::string MetaProfiler::_getCPUName() {
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
    while (std::getline(resstream, segment, ':')) {
        seglist.push_back(segment);
    }

    if (segment.length() >= 2) {
        auto lastchar = segment[segment.length()-1];
        auto firstchar = segment[0];

        if (lastchar == '\n') {
            segment.erase(segment.length()-1);
        }

        if (firstchar == ' ') {
            segment.erase(0, 1);
        }
    }

    return  segment;
}

std::string MetaProfiler::_getArchitecture() {
    return _readSystemFile("cat /proc/cpuinfo | grep 'cpu family' | uniq");
}

std::string MetaProfiler::_getNumberOfCores() {
    return _readSystemFile("cat /proc/cpuinfo | grep 'siblings' | uniq");
}

double MetaProfiler::_getRaplUnit() {
    RegisterReader powReader(0);
    double unit = powReader.readMultiplier();

    return unit;
}

std::string MetaProfiler::_readSystemFile(std::string file) {
    char buffer[128];
    std::string result;

    FILE* pipe = popen(file.c_str(), "r");
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
    while ( std::getline(resstream, segment, ':') ) {
        seglist.push_back(segment);
    }

    if ( segment.length() >=2 ) {
        auto lastchar = segment[segment.length()-1];
        auto firstchar = segment[0];

        if (lastchar == '\n') {
            segment.erase(segment.length()-1);
        }

        if (firstchar == ' ') {
            segment.erase(0, 1);
        }
    }

    return segment;
}

std::string MetaProfiler::_getTimeStr() {
    std::chrono::time_point<std::chrono::system_clock> timepoint = std::chrono::system_clock::now();
    uint64_t tpstr = timepoint.time_since_epoch().count();
    return std::to_string(tpstr);
}


