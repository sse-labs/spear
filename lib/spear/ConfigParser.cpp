/*
 * Copyright (c) 2026 Maximilian Krebs
 * All rights reserved.
 */

#include "ConfigParser.h"

#include <fstream>
#include <iostream>
#include <string>
#include <vector>

AnalysisConfiguration ConfigParser::analysisConfiguration = {};
ProfilingConfiguration ConfigParser::profilingConfiguration = {};

ConfigParser::ConfigParser(const std::string& path) {
    this->read(path);
}

void ConfigParser::read(const std::string &fileName) {
    json data;

    std::ifstream fileStream(fileName);

    try {
        data = json::parse(fileStream);
        config = data;
    }catch (const json::parse_error& e) {
        std::cout << "Failed to parse config file: " << e.what() << std::endl;
        config = json();
    }
}

json ConfigParser::getConfig() {
    return config;
}

bool ConfigParser::configValid() {
    if (!config.empty()) {
        if (profilingValid() &&
            analysisValid()) {
            return true;
        }
        std::cout << "Invalid config: profiling or analysis section invalid." << std::endl;
        return false;
    }

    std::cout << "Invalid config: empty configuration." << std::endl;
    return false;
}

bool ConfigParser::fallbackValid(json object) {
    if (object.contains("fallback")) {
        auto fallback = object["fallback"];

        if (!fallback.is_object()) {
            std::cout << "Invalid analysis.fallback: not an object." << std::endl;
            return false;
        }

        const std::vector<std::string> requiredKeys = {
            "MALFORMED_LOOP", "SYMBOLIC_BOUND_LOOP", "NON_COUNTING_LOOP",
            "NESTED_LOOP", "UNKNOWN_LOOP"
        };

        for (const auto& key : requiredKeys) {
            if (!fallback.contains(key) ||
                !fallback[key].is_number_unsigned() ||
                fallback[key] <= 0) {
                std::cout << "Invalid analysis.fallback: missing or "
                          << "non-positive loop fallback values." << std::endl;
                return false;
            }
        }
            return true;
        }

    std::cout << "Invalid analysis: missing fallback section." << std::endl;
    return false;
}

bool ConfigParser::strategyValid(json object) {
    if (object.contains("strategy") && object["strategy"].is_string()) {
        std::string strategy = object["strategy"];
        auto strat = strToStrategy(strategy);

        if (strat != Strategy::UNDEFINED) {
            return true;
        }
        std::cout << "Invalid analysis.strategy: unsupported value." << std::endl;
        return false;
    }

    std::cout << "Invalid analysis.strategy: missing or not a string." << std::endl;
    return false;
}

bool ConfigParser::modeValid(json object) {
    if (object.contains("mode") && object["mode"].is_string()) {
        std::string mode = object["mode"];
        auto m = strToMode(mode);

        if (m != Mode::UNDEFINED) {
            return true;
        }
        std::cout << "Invalid analysis.mode: unsupported value." << std::endl;
        return false;
    }

    std::cout << "Invalid analysis.mode: missing or not a string." << std::endl;
    return false;
}

bool ConfigParser::formatValid(json object) {
    if (object.contains("format") && object["format"].is_string()) {
        std::string format = object["format"];
        auto fmt = strToFormat(format);

        if (fmt != Format::UNDEFINED) {
            return true;
        }
        std::cout << "Invalid analysis.format: unsupported value." << std::endl;
        return false;
    }

    std::cout << "Invalid analysis.format: missing or not a string." << std::endl;
    return false;
}

bool ConfigParser::iterationsValid(json object) {
    if (object.contains("iterations") && object["iterations"].is_number()) {
        if (object["iterations"] > 0) {
            return true;
        }
        std::cout << "Invalid profiling.iterations: must be > 0." << std::endl;
        return false;
    }

    std::cout << "Invalid profiling.iterations: missing or not numeric." << std::endl;
    return false;
}

bool ConfigParser::profilingValid() {
    if (config.contains("profiling")) {
        auto profiling = config["profiling"];

        if (profiling.is_object()) {
            return iterationsValid(profiling);
        }
        std::cout << "Invalid profiling: not an object." << std::endl;
        return false;
    }

    std::cout << "Invalid config: missing profiling section." << std::endl;
    return false;
}

bool ConfigParser::analysisValid() {
    if (config.contains("analysis")) {
        auto analysis = config["analysis"];

        if (analysis.is_object()) {
            bool fallbackOk = fallbackValid(analysis);
            bool modeOk = modeValid(analysis);
            bool formatOk = formatValid(analysis);
            bool strategyOk = strategyValid(analysis);

            if (fallbackOk && modeOk && formatOk && strategyOk) {
                return true;
            }
            std::cout << "Invalid analysis: one or more properties are invalid." << std::endl;
            return false;
        }

        std::cout << "Invalid analysis: not an object." << std::endl;
        return false;
    }

    std::cout << "Invalid config: missing analysis section." << std::endl;
    return false;
}

Mode ConfigParser::strToMode(const std::string& str) {
    if (str == "program") {
        return Mode::PROGRAM;
    } else if (str == "function") {
        return Mode::FUNCTION;
    } else if (str == "instruction") {
        return Mode::INSTRUCTION;
    } else if (str == "block") {
        return Mode::BLOCK;
    } else {
        return Mode::UNDEFINED;
    }
}

Strategy ConfigParser::strToStrategy(const std::string &str) {
    if (str == "worst") {
        return Strategy::WORST;
    } else if (str == "average") {
        return Strategy::AVERAGE;
    } else if (str == "worst") {
        return Strategy::WORST;
    } else {
        return Strategy::UNDEFINED;
    }
}

Format ConfigParser::strToFormat(const std::string &str) {
    if (str == "plain") {
        return Format::PLAIN;
    } else if (str == "json") {
        return Format::JSON;
    } else {
        return Format::UNDEFINED;
    }
}

AnalysisConfiguration ConfigParser::getAnalysisConfiguration() {
    return analysisConfiguration;
}

ProfilingConfiguration ConfigParser::getProfilingConfiguration() {
    return profilingConfiguration;
}

void ConfigParser::parse() {
    if (config.empty()) {
        return;
    }

    if (analysisValid()) {
        auto analysis = config["analysis"];

        analysisConfiguration.mode = strToMode(analysis["mode"].get<std::string>());
        analysisConfiguration.format = strToFormat(analysis["format"].get<std::string>());
        analysisConfiguration.strategy = strToStrategy(analysis["strategy"].get<std::string>());
        analysisConfiguration.deepcalls = DeepCalls::UNDEFINED;

        analysisConfiguration.fallback.clear();
        if (analysis.contains("fallback") && analysis["fallback"].is_object()) {
            for (auto it = analysis["fallback"].begin(); it != analysis["fallback"].end(); ++it) {
                analysisConfiguration.fallback[it.key()] = it.value().get<double>();
            }
        }
    }

    if (profilingValid()) {
        auto profiling = config["profiling"];
        profilingConfiguration.iterations = profiling["iterations"].get<int>();
    }
}
