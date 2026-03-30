/*
 * Copyright (c) 2026 Maximilian Krebs
 * All rights reserved.
 */

#include "ILP/ILPClusterCache.h"

#include <filesystem>
#include <fstream>
#include <iostream>
#include <nlohmann/json.hpp>
#include <utility>

#include "profilers/CPUProfiler.h"

ILPClusterCache* ILPClusterCache::instance = nullptr;


ILPClusterCache& ILPClusterCache::getInstance() {
    if (instance == nullptr) {
        throw std::runtime_error("ILPClusterCache not initialized");
    }

    return *instance;
}

ILPClusterCache::ILPClusterCache(std::string filename, bool enabled) {
    cacheFile = std::move(filename);
    isEnabled = enabled;

    // Check if file exists
    if (!std::filesystem::exists(cacheFile)) {
        // Create empty JSON file
        nlohmann::json emptyJson = nlohmann::json::object();

        std::ofstream outputFile(cacheFile);
        if (!outputFile.is_open()) {
            throw std::runtime_error("Failed to create cache file: " + cacheFile);
        }

        outputFile << emptyJson.dump(4); // Pretty print with indentation
        outputFile.close();

        // Initialize internal cache state as empty
        cache = {};
    } else {
        // File exists → read JSON
        std::ifstream inputFile(cacheFile);
        if (!inputFile.is_open()) {
            throw std::runtime_error("Failed to open cache file: " + cacheFile);
        }

        try {
            json data = json::parse(inputFile);

            for (const auto& entry : data.items()) {
                const auto& value = entry.value();

                // Validate structure explicitly before accessing
                if (!value.contains("optimalValue") || !value.contains("variableValues")) {
                    continue;
                }

                ILPResult result;
                result.optimalValue = value.at("optimalValue").get<double>();

                // Reconstruct the variable values
                int maxVal = value.at("variableCount").get<int>();
                std::vector<std::pair<int, double>> nonEmptyEntries = value.at("variableValues").get<std::vector<std::pair<int, double>>>();

                std::vector variables(maxVal, 0.0);
                for (const auto& [index, varValue] : nonEmptyEntries) {
                    variables[index] = varValue;
                }

                result.variableValues = variables;

                cache[entry.key()] = result;
            }

        } catch (const nlohmann::json::parse_error& parseException) {
            std::cerr << "Cache parse error: " << parseException.what() << "\n";
            cache = {};
        } catch (const nlohmann::json::type_error& typeException) {
            std::cerr << "Cache type error: " << typeException.what() << "\n";
            cache = {};
        } catch (const nlohmann::json::out_of_range& rangeException) {
            std::cerr << "Cache schema error (missing field): " << rangeException.what() << "\n";
            cache = {};
        }

        inputFile.close();
    }

    instance = this;
}

bool ILPClusterCache::entryExists(std::string hash) {
    // If caching is disabled. We always assume, that the entry does not exist!
    if (!isEnabled) {
        return false;
    }

    return cache.find(hash) != cache.end();
}

std::optional<ILPResult> ILPClusterCache::getEntry(const std::string& hash) {
    auto iterator = cache.find(hash);
    if (iterator != cache.end()) {
        return std::optional<ILPResult>(iterator->second);
    }

    return std::nullopt;
}

void ILPClusterCache::setEntry(const std::string& hash, ILPResult value) {
    cache[hash] = std::move(value);
}

void ILPClusterCache::writeBackCache() {
    if (!isEnabled) {
        return;
    }

    nlohmann::json jsonData = nlohmann::json::object();

    for (const auto &entry : cache) {
        std::vector<std::pair<int, double>> nonEmptyEntries;

        // Compress the values of the variables. We only store values that are not 0
        for (size_t i = 0; i < entry.second.variableValues.size(); ++i) {
            if (entry.second.variableValues[i] > 0.0) {
                nonEmptyEntries.emplace_back(i, entry.second.variableValues[i]);
            }
        }

        jsonData[entry.first] = json::object({
            {"optimalValue", entry.second.optimalValue},
            {"variableCount", entry.second.variableValues.size()},
            {"variableValues", nonEmptyEntries}
        });
    }

    std::ofstream outputFile(cacheFile);
    if (!outputFile.is_open()) {
        throw std::runtime_error("Failed to open cache file for writing: " + cacheFile);
    }

    // We need to remove the value here to safe space...
    outputFile << jsonData.dump(4);
    outputFile.close();
}
