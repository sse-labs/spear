/*
 * Copyright (c) 2026 Maximilian Krebs
 * All rights reserved.
 */

#include "ELBs/ELBParser.h"

#include <fstream>

#include "Logger.h"
#include "nlohmann/json.hpp"

std::unordered_map<std::string, double> ELBParser::parseELBFile(const std::string &filename) {
    // Initialize JSON container
    nlohmann::json parsedJsonData;

    // Open file stream
    std::ifstream inputFileStream(filename);

    // Validate that file exists and can be opened
    if (!inputFileStream.is_open()) {
        Logger::getInstance().log("Failed to open ELB file: " + filename, LOGLEVEL::WARNING);
        return {};
    }

    // Parse JSON with error handling
    try {
        inputFileStream >> parsedJsonData;
    } catch (const nlohmann::json::parse_error &parseError) {
        Logger::getInstance().log(
            "JSON parse error in file " + filename + ": " + std::string(parseError.what()),
            LOGLEVEL::WARNING
        );
        return {};
    }

    // Validate JSON structure (must be object)
    if (!parsedJsonData.is_object()) {
        Logger::getInstance().log(
            "Invalid ELB file format (root is not object): " + filename,
            LOGLEVEL::WARNING
        );
        return {};
    }

    std::unordered_map<std::string, double> extractedMapping;

    // Extract key-value pairs
    for (const auto& [key, value] : parsedJsonData.items()) {
        if (!value.is_number()) {
            Logger::getInstance().log(
                "Invalid value for key '" + key + "' in " + filename + " (expected number)",
                LOGLEVEL::WARNING
            );
            continue;
        }

        extractedMapping[key] = value.get<double>();
    }

    return extractedMapping;
}
