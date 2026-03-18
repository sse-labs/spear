/*
 * Copyright (c) 2026 Maximilian Krebs
 * All rights reserved.
*/

#include <string>
#include <fstream>
#include "iostream"
#include "ProfileHandler.h"

ProfileHandler& ProfileHandler::get_instance() {
    static ProfileHandler instance;
    return instance;
}

ProfileHandler::ProfileHandler() = default;

// Read the data from the provided file
void ProfileHandler::read(const std::string& filename) {
    // init reader and data
    json data;

    // Create a filestream to the provided file
    std::ifstream fileStream(filename);

    // Move the data from the stream to our initialized JSON::Value object
    data = json::parse(fileStream);

    _profile = data;
}

void ProfileHandler::setOrCreate(std::string key, json &mapping) {
    _profile[key]= mapping;
}

json ProfileHandler::getProfile() {
    return _profile;
}

void ProfileHandler::write(const std::string& filename) {
    //  Create a filestream and open it
    std::ofstream fileStream;
    fileStream.open(filename);

    if (fileStream.is_open()) {
        // Use the json dump method to write the data to the filestream
        fileStream << _profile.dump(4);
        fileStream.close();
    } else {
        std::cout << "ERROR opening the file" << "\n";
    }
}

std::optional<double> ProfileHandler::getEnergyForInstruction(const std::string& instruction) {
    if (_profile["cpu"].contains(instruction)) {
        return _profile["cpu"][instruction].get<double>();
    } else {
        return std::nullopt;
    }
}

std::optional<double> ProfileHandler::getProgramOffset() {
    if (_profile["cpu"].contains("_programoffset")) {
        return _profile["cpu"]["_programoffset"].get<double>();
    } else {
        return std::nullopt;
    }
}

std::optional<double> ProfileHandler::getUnknownCost() {
    if (_profile["cpu"].contains("_unknown_cost")) {
        return _profile["cpu"]["_unknown_cost"].get<double>();
    } else {
        return std::nullopt;
    }
}

std::optional<double> ProfileHandler::getEnergyForSyscall(const std::string& syscall) {
    if (_profile["syscalls"].contains(syscall)) {
        return _profile["syscalls"][syscall].get<double>();
    } else {
        return std::nullopt;
    }
}
