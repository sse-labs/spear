/*
 * Copyright (c) 2026 Maximilian Krebs
 * All rights reserved.
 */

#ifndef SRC_SPEAR_CONFIGPARSER_H_
#define SRC_SPEAR_CONFIGPARSER_H_


#include <string>
#include <map>

#include <nlohmann/json.hpp>

using json = nlohmann::json;

#define ULTIMATEFALLBACK 10000

/**
 * Enum to distinguish the analysis target in the application
 */
enum class Mode {
    UNDEFINED,
    PROGRAM,
    BLOCK,
    FUNCTION,
    INSTRUCTION,
    GRAPH
};

/**
 * Enum to distinguish the analysis target in the application
 */
enum class DeepCalls {
    UNDEFINED,
    ENABLED,
};

/**
 * Enum used to specify the output format
 */
enum class Format {
    UNDEFINED,
    PLAIN,
    JSON
};

/**
 * Enum describing the analysis strategy
 */
enum class Strategy {
    UNDEFINED,
    WORST,
    AVERAGE,
    BEST
};


struct AnalysisConfiguration {
    Mode mode;
    Format format;
    Strategy strategy;
    DeepCalls deepcalls;
    std::map<std::string, int64_t> fallback;
};

struct ProfilingConfiguration {
    int iterations;
};

class ConfigParser {
 public:
    explicit ConfigParser(const std::string& path);

    void read(const std::string & fileName);

    bool configValid();

    json getConfig();

    static AnalysisConfiguration getAnalysisConfiguration();
    static ProfilingConfiguration getProfilingConfiguration();

    void parse();

 private:
    json config;
    static AnalysisConfiguration analysisConfiguration;
    static ProfilingConfiguration profilingConfiguration;

    bool profilingValid();

    bool analysisValid();

    bool fallbackValid(json object);

    bool modeValid(json object);

    bool formatValid(json object);

    bool strategyValid(json object);

    bool iterationsValid(json object);

    /**
     * Convert a string to mode enum type
     *
     * @param str String to convert
     * @return Mode enum type
     */
    static Mode strToMode(const std::string& str);

    /**
     * Convert a string to format enum type
     *
     * @param str String to convert
     * @return Format enum type
     */
    static Format strToFormat(const std::string& str);

    /**
     * Convert a string to strategy enum type
     *
     * @param str String to convert
     * @return Strategy enum type
     */
    static Strategy strToStrategy(const std::string& str);
};

#endif  // SRC_SPEAR_CONFIGPARSER_H_
