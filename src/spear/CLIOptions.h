/*
 * Copyright (c) 2026 Maximilian Krebs
 * All rights reserved.
*/

#ifndef SRC_SPEAR_CLIOPTIONS_H_
#define SRC_SPEAR_CLIOPTIONS_H_

#include <string>

/**
 * Enum to handle the operations available in the application
 */
enum class Operation {
    UNDEFINED,
    ANALYZE,
    PROFILE
};


/**
 * CLIOptions class to encapsulate the parsed command line arguments
 */
class CLIOptions {
 public:

    /**
     * Path where profile should be saved
     */
    std::string saveLocation;

    /**
     * Path where the profile should be read from
     */
    std::string profilePath;

    /**
     * Parsed operation
     */
    Operation operation;

    /**
     * Parsed config path;
     */
    std::string configPath;

    /**
     * Path where the program should be read from
     */
    std::string programPath;

    /**
     * Path where the profile should be read from
     */
    std::string codePath;

    /**
     * Construct a new CLIOptions object
     * 
     */
    CLIOptions();
};

/**
 * Subclass to distinguish options related to profiling
 * 
 */
class ProfileOptions : public CLIOptions{
 public:
    ProfileOptions(std::string codePath, std::string configPath, std::string saveLocation);
};

/**
 * Subclass to distinguish options related to the analysis
 * 
 */
class AnalysisOptions : public CLIOptions{
 public:
    AnalysisOptions(std::string profilePath, std::string configPath, std::string programPath);
};


#endif  // SRC_SPEAR_CLIOPTIONS_H_
