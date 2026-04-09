/*
 * Copyright (c) 2026 Maximilian Krebs
 * All rights reserved.
 */

#include "OutputHandler.h"

#include <fstream>

#include "ConfigParser.h"

void OutputHandler::writeJsonOutput(std::string filename, nlohmann::json content, bool writeMultiple) {
    if (writeMultiple) {

    } else {
        writeJsonFile(filename + ".json", content);
    }
}

void OutputHandler::writeELBOutput(std::string filename, std::unordered_map<std::string, double> content, bool writeMultiple) {
    if (writeMultiple) {

    } else {
        writeELBFile(filename + ".elb", content);
    }
}

void OutputHandler::writeELBFile(std::string filename, nlohmann::json content) {
    const std::string outputDirectory = ConfigParser::getAnalysisConfiguration().outputDirectory;
    const std::filesystem::path outputDirectoryPath(outputDirectory);
    const std::filesystem::path filePath = outputDirectoryPath / filename;

    try {
        std::filesystem::create_directories(outputDirectoryPath);

        std::ofstream outputFile(filePath);

        if (!outputFile.is_open()) {
            throw std::runtime_error("Failed to open file for writing: " + filePath.string());
        }

        // Write JSON with indentation (4 spaces)
        outputFile << content.dump(4);

        outputFile.close();
    }
    catch (const std::exception& exception) {
        throw std::runtime_error(std::string("Error writing JSON file: ") + exception.what());
    }
}

void OutputHandler::writeJsonFile(std::string filename, nlohmann::json content) {
    const std::string outputDirectory = ConfigParser::getAnalysisConfiguration().outputDirectory;
    const std::filesystem::path outputDirectoryPath(outputDirectory);
    const std::filesystem::path filePath = outputDirectoryPath / filename;

    try {
        std::filesystem::create_directories(outputDirectoryPath);

        std::ofstream outputFile(filePath);

        if (!outputFile.is_open()) {
            throw std::runtime_error("Failed to open file for writing: " + filePath.string());
        }

        // Write JSON with indentation (4 spaces)
        outputFile << content.dump(4);

        outputFile.close();
    }
    catch (const std::exception& exception) {
        throw std::runtime_error(std::string("Error writing JSON file: ") + exception.what());
    }
}


std::string OutputHandler::getFileNameFromAnalysisType(AnalysisType type) {
    switch (type) {
        case AnalysisType::MONOLITHIC:
            return "monolithic";
        case AnalysisType::CLUSTERED:
            return "clustered";
        case AnalysisType::LEGACY:
            return "legacy";
        default:
            return "";
    }
}
