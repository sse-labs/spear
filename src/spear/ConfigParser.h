/*
 * Copyright (c) 2026 Maximilian Krebs
 * All rights reserved.
 */

#ifndef SRC_SPEAR_CONFIGPARSER_H_
#define SRC_SPEAR_CONFIGPARSER_H_

#include "configuration/valuespace.h"

#include <string>
#include <map>

#include <nlohmann/json.hpp>

#include "configuration/configurationobjects.h"


using json = nlohmann::json;

#define ULTIMATEFALLBACK 10000

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


    bool legacyValid(json object);

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
    bool analysisTypeValid(json object);

    /**
     * Validate the analysis strategy configuration section.
     *
     * @param object JSON object containing strategy data
     * @return True if valid, otherwise false
     */
    bool strategyValid(json object);

    bool minProgramEnergy(json object);
    bool minInstructionEnergy(json object);
    bool CPURegressionValid(json object);
    bool SyscallProfilingConfigValid(json object);
};

#endif  // SRC_SPEAR_CONFIGPARSER_H_
