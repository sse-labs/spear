/*
 * Copyright (c) 2026 Maximilian Krebs
 * All rights reserved.
 */

#include "ConfigParser.h"

#include <fstream>
#include <iostream>
#include <string>
#include <vector>

#include "configuration/configurationUtils.h"

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
    if (!object.contains("fallback")) {
        std::cout << "Invalid analysis: missing fallback section." << std::endl;
        return false;
    }

    auto fallback = object["fallback"];

    if (!fallback.is_object()) {
        std::cout << "Invalid analysis.fallback: not an object." << std::endl;
        return false;
    }

    if (!fallback.contains("calls") || !fallback["calls"].is_object()) {
        std::cout << "Invalid analysis.fallback.calls: missing or not an object." << std::endl;
        return false;
    }

    if (!fallback.contains("loops") || !fallback["loops"].is_object()) {
        std::cout << "Invalid analysis.fallback.loops: missing or not an object." << std::endl;
        return false;
    }

    auto callsFallback = fallback["calls"];
    auto loopsFallback = fallback["loops"];

    if (!callsFallback.contains("UNKNOWN_FUNCTION") ||
        !callsFallback["UNKNOWN_FUNCTION"].is_number() ||
        callsFallback["UNKNOWN_FUNCTION"].get<double>() <= 0.0) {
            std::cout << "Invalid analysis.fallback.calls: missing or non-positive UNKNOWN_FUNCTION value." << std::endl;
            return false;
    }

    const std::vector<std::string> requiredLoopKeys = {
        "MALFORMED_LOOP",
        "SYMBOLIC_BOUND_LOOP",
        "NON_COUNTING_LOOP",
        "NESTED_LOOP",
        "UNKNOWN_LOOP"
    };

    for (const auto& key : requiredLoopKeys) {
        if (!loopsFallback.contains(key) ||
            !loopsFallback[key].is_number() ||
            loopsFallback[key].get<double>() <= 0.0) {
            std::cout << "Invalid analysis.fallback.loops: missing or non-positive value for key '"
                      << key << "'." << std::endl;
            return false;
            }
    }

    return true;
}

bool ConfigParser::strategyValid(json object) {
    if (object.contains("strategy") && object["strategy"].is_string()) {
        std::string strategy = object["strategy"];
        auto strat = ConfigurationUtils::strToStrategy(strategy);

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
        auto m = ConfigurationUtils::strToMode(mode);

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
        auto fmt = ConfigurationUtils::strToFormat(format);

        if (fmt != Format::UNDEFINED) {
            return true;
        }
        std::cout << "Invalid analysis.format: unsupported value." << std::endl;
        return false;
    }

    std::cout << "Invalid analysis.format: missing or not a string." << std::endl;
    return false;
}

bool ConfigParser::analysisTypeValid(json object) {
    if (object.contains("type") && object["type"].is_string()) {
        std::string type = object["type"];
        auto tpe = ConfigurationUtils::strToAnalysisType(type);

        if (tpe != AnalysisType::UNDEFINED) {
            return true;
        }
        std::cout << "Invalid analysis.type: unsupported value." << std::endl;
        return false;
    }

    std::cout << "Invalid analysis.format: missing or not a string." << std::endl;
    return false;
}

bool ConfigParser::minProgramEnergy(json object) {
    if (object.contains("min_program_energy") && object["min_program_energy"].is_number()) {
        if (object["min_program_energy"] >= 0.0) {
            return true;
        }
        std::cout << "Invalid profiling.min_energy: must be >= 0." << std::endl;
        return false;
    }

    std::cout << "Invalid profiling.min_energy: missing or not numeric." << std::endl;
    return false;
}

bool ConfigParser::minInstructionEnergy(json object) {
    if (object.contains("min_instruction_energy") && object["min_instruction_energy"].is_number()) {
        if (object["min_instruction_energy"] >= 0) {
            return true;
        }
        std::cout << "Invalid profiling.min_instruction_energy: must be >= 0.0" << std::endl;
        return false;
    }

    std::cout << "Invalid profiling.slope_filter: missing or not numeric." << std::endl;
    return false;
}

bool ConfigParser::CPURegressionValid(json object) {
    if (object.contains("cpu_regression") && object["cpu_regression"].is_object()) {
        auto cpuregression = object["cpu_regression"];

        if (cpuregression.contains("limit") && cpuregression["limit"].is_number_unsigned() &&
            cpuregression.contains("step") && cpuregression["step"].is_number_unsigned() &&
            cpuregression.contains("offset") && cpuregression["offset"].is_number_unsigned()) {
            return true;
        }
        std::cout << "Invalid profiling.cpu_regression: missing or invalid properties." << std::endl;
        return false;
    }

    std::cout << "Invalid profiling.cpu_regression: missing or not an object." << std::endl;
    return false;
}

bool ConfigParser::SyscallProfilingConfigValid(json object) {
    if (object.contains("syscalls") && object["syscalls"].is_object()) {
        auto syscallconfig = object["syscalls"];

        if (syscallconfig.contains("runtime") && syscallconfig["runtime"].is_number_unsigned() &&
            syscallconfig.contains("default_energy") && syscallconfig["default_energy"].is_number() &&
            syscallconfig.contains("max_syscall_id") && syscallconfig["max_syscall_id"].is_number_unsigned()) {
            return true;
        }
        std::cout << "Invalid profiling.syscalls: missing or invalid properties." << std::endl;
        return false;
    }

    std::cout << "Invalid profiling.syscalls: missing or not an object." << std::endl;
    return false;
}

bool ConfigParser::profilingValid() {
    if (config.contains("profiling")) {
        auto profiling = config["profiling"];

        if (profiling.is_object()) {
            return minProgramEnergy(profiling) &&
                minInstructionEnergy(profiling) &&
                CPURegressionValid(profiling) &&
                SyscallProfilingConfigValid(profiling);
        }
        std::cout << "Invalid profiling: not an object." << std::endl;
        return false;
    }

    std::cout << "Invalid config: missing profiling section." << std::endl;
    return false;
}

bool ConfigParser::legacyValid(json object) {
    if (object.contains("legacyConfig") && object["legacyConfig"].is_object()) {
        auto legacyconfig = object["legacyConfig"];

        if (legacyconfig.contains("mode") && modeValid(legacyconfig) &&
            legacyconfig.contains("format") && formatValid(legacyconfig) &&
            legacyconfig.contains("strategy") && strategyValid(legacyconfig)) {
            return true;
        }
        std::cout << "Invalid legacyconfig: missing or invalid properties." << std::endl;
        return false;
    }

    std::cout << "Invalid config: missing legacy section." << std::endl;
    return false;
}

bool ConfigParser::outputDirValid(json object) {
    if (!object.contains("outputDirectory") || !object["outputDirectory"].is_string()) {
        return false;
    }

    const std::string outputDirectoryString = object["outputDirectory"];
    const std::filesystem::path outputDirectoryPath(outputDirectoryString);

    try {
        // Path exists
        if (std::filesystem::exists(outputDirectoryPath)) {
            // Must be a directory
            if (!std::filesystem::is_directory(outputDirectoryPath)) {
                return false;
            }
        } else {
            // Create directory (including parent directories)
            if (!std::filesystem::create_directories(outputDirectoryPath)) {
                return false;
            }
        }

        const std::filesystem::path testFilePath = outputDirectoryPath / ".permission_test";
        std::ofstream testFile(testFilePath.string());

        if (!testFile.is_open()) {
            return false;
        }

        testFile.close();
        std::filesystem::remove(testFilePath);

        return true;
    }
    catch (const std::filesystem::filesystem_error&) {
        return false;
    }
}

bool ConfigParser::analysisValid() {
    if (config.contains("analysis")) {
        auto analysis = config["analysis"];

        if (analysis.is_object()) {
            bool outputDirOk = outputDirValid(analysis);
            bool fallbackOk = fallbackValid(analysis);
            bool legacyOk = legacyValid(analysis) || analysis["type"] != "legacy";

            if (outputDirOk && fallbackOk && legacyOk) {
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
        auto legacyconfig = analysis["legacyConfig"];

        analysisConfiguration.analysisType = ConfigurationUtils::strToAnalysisType(
            analysis["type"].get<std::string>());
        analysisConfiguration.analysisOutputMode = ConfigurationUtils::strToAnalysisOutputmode(
            analysis["outputmode"].get<std::string>());
        analysisConfiguration.outputDirectory = analysis["outputDirectory"].get<std::string>();
        analysisConfiguration.cachingEnabled = analysis["clusteredCacheEnabled"].get<bool>();
        analysisConfiguration.feasibilityEnabled = analysis["feasibilityEnabled"].get<bool>();
        analysisConfiguration.writeDotFiles = analysis["writeDotFiles"].get<bool>();

        analysisConfiguration.legacyconfig.mode = ConfigurationUtils::strToMode(
            legacyconfig["mode"].get<std::string>());
        analysisConfiguration.legacyconfig.format = ConfigurationUtils::strToFormat(
            legacyconfig["format"].get<std::string>());
        analysisConfiguration.legacyconfig.strategy = ConfigurationUtils::strToStrategy(
            legacyconfig["strategy"].get<std::string>());
        analysisConfiguration.legacyconfig.deepcalls = false;

        // Clear previous fallback configuration
        analysisConfiguration.fallback.clear();

        if (analysis.contains("fallback") && analysis["fallback"].is_object()) {
            const auto& fallback = analysis["fallback"];

            for (auto categoryIt = fallback.begin(); categoryIt != fallback.end(); ++categoryIt) {
                const std::string& categoryName = categoryIt.key();
                const auto& categoryObject = categoryIt.value();

                // Ensure category is an object
                if (!categoryObject.is_object()) {
                    continue;
                }

                for (auto valueIt = categoryObject.begin(); valueIt != categoryObject.end(); ++valueIt) {
                    const std::string& key = valueIt.key();
                    double value = valueIt.value().get<double>();

                    analysisConfiguration.fallback[categoryName][key] = value;
                }
            }
        }
    }

    if (profilingValid()) {
        auto profiling = config["profiling"];
        profilingConfiguration.min_program_energy   = profiling["min_program_energy"].get<double>();
        profilingConfiguration.min_instruction_energy = profiling["min_instruction_energy"].get<double>();

        profilingConfiguration.cpuregression.limit = profiling["cpu_regression"]["limit"].get<int>();
        profilingConfiguration.cpuregression.step = profiling["cpu_regression"]["step"].get<int>();
        profilingConfiguration.cpuregression.offset = profiling["cpu_regression"]["offset"].get<int>();

        profilingConfiguration.syscallconfig.runtime = profiling["syscalls"]["runtime"].get<int>();
        profilingConfiguration.syscallconfig.defaultEnergy = profiling["syscalls"]["default_energy"].get<double>();
        profilingConfiguration.syscallconfig.maxSyscallId = profiling["syscalls"]["max_syscall_id"].get<int>();
    }
}
