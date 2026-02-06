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


/**
 * Holds analysis-related configuration options parsed from the config file.
 */
struct AnalysisConfiguration {
    Mode mode;
    Format format;
    Strategy strategy;
    DeepCalls deepcalls;
    std::map<std::string, int64_t> fallback;
};

/**
 * Holds profiling-related configuration options parsed from the config file.
 */
struct ProfilingConfiguration {
    int iterations;
};

class ConfigParser {
 public:
    /**
     * Construct a parser for the given configuration file path.
     *
     * @param path Path to the configuration file
     */
    explicit ConfigParser(const std::string& path);

    /**
     * Read the configuration file into the parser.
     *
     * @param fileName Path to the configuration file
     */
    void read(const std::string & fileName);

    /**
     * Check whether the currently loaded configuration is valid.
     *
     * @return True if valid, otherwise false
     */
    bool configValid();

    /**
     * Get the raw JSON configuration.
     *
     * @return Parsed JSON object
     */
    json getConfig();

    /**
     * Get the parsed analysis configuration.
     *
     * @return AnalysisConfiguration instance
     */
    static AnalysisConfiguration getAnalysisConfiguration();

    /**
     * Get the parsed profiling configuration.
     *
     * @return ProfilingConfiguration instance
     */
    static ProfilingConfiguration getProfilingConfiguration();

    /**
     * Parse the loaded JSON into typed configuration structs.
     */
    void parse();

 private:
    json config;
    static AnalysisConfiguration analysisConfiguration;
    static ProfilingConfiguration profilingConfiguration;

    /**
     * Validate the profiling configuration section.
     *
     * @return True if valid, otherwise false
     */
    bool profilingValid();

    /**
     * Validate the analysis configuration section.
     *
     * @return True if valid, otherwise false
     */
    bool analysisValid();

    /**
     * Validate the fallback configuration section.
     *
     * @param object JSON object containing fallback data
     * @return True if valid, otherwise false
     */
    bool fallbackValid(json object);

    /**
     * Validate the analysis mode configuration section.
     *
     * @param object JSON object containing mode data
     * @return True if valid, otherwise false
     */
    bool modeValid(json object);

    /**
     * Validate the output format configuration section.
     *
     * @param object JSON object containing format data
     * @return True if valid, otherwise false
     */
    bool formatValid(json object);

    /**
     * Validate the analysis strategy configuration section.
     *
     * @param object JSON object containing strategy data
     * @return True if valid, otherwise false
     */
    bool strategyValid(json object);

    /**
     * Validate the profiling iterations configuration section.
     *
     * @param object JSON object containing iteration data
     * @return True if valid, otherwise false
     */
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
