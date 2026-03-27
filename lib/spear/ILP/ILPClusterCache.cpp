/*
 * Copyright (c) 2026 Maximilian Krebs
 * All rights reserved.
 */

#include "ILP/ILPClusterCache.h"

#include <filesystem>
#include <fstream>
#include <iostream>
#include <nlohmann/json.hpp>

#include "profilers/CPUProfiler.h"

ILPClusterCache* ILPClusterCache::instance = nullptr;


ILPClusterCache& ILPClusterCache::getInstance() {
    if (instance == nullptr) {
        throw std::runtime_error("ILPClusterCache not initialized");
    }

    return *instance;
}

ILPClusterCache::ILPClusterCache(std::string filename, bool enabled) {
    cacheFile = filename;
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
                result.variableValues = value.at("variableValues").get<std::vector<double>>();

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

void ILPClusterCache::setEntry(std::string hash, ILPResult value) {
    cache[hash] = value;
}

void ILPClusterCache::writeBackCache() {
    if (!isEnabled) {
        return;
    }

    nlohmann::json jsonData = nlohmann::json::object();

    for (const auto &entry : cache) {
        jsonData[entry.first] = json::object({
            {"optimalValue", entry.second.optimalValue},
            {"variableValues", entry.second.variableValues}
        });
    }

    std::ofstream outputFile(cacheFile);
    if (!outputFile.is_open()) {
        throw std::runtime_error("Failed to open cache file for writing: " + cacheFile);
    }

    outputFile << jsonData.dump(0);
    outputFile.close();
}
