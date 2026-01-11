/*
 * Copyright (c) 2026 Maximilian Krebs
 * All rights reserved.
*/

#ifndef SRC_SPEAR_CLIHANDLER_H_
#define SRC_SPEAR_CLIHANDLER_H_


#include "CLIOptions.h"
#include <vector>
#include <string>

/**
 * Class for dealing with CLI input
 */
class CLIHandler {
 public:
    // We only want to use the class statically
    CLIHandler(const CLIHandler&)            = delete;
    CLIHandler& operator=(const CLIHandler&) = delete;

    /**
     * Parse a given array of arguments. Is delimited by argc arguments 
     * 
     * @param argc Number of arguments
     * @param argv Argument array
     * @return CLIOptions Object containing the CLI options parsed
     */
    static CLIOptions parseCLI(int argc, char *argv[]);

 private:
    /**
     * Checks if a vector of arguments contains a certain option
     * 
     * @param arguments Vector of arguments
     * @param option_name Argument to check for
     * @return true If the argument is present
     * @return false otherwise
     */
    static bool hasOption(
            const std::vector<std::string_view>& arguments,
            const std::string_view& option_name);

    /**
     * Get a certain argument from the given vector of arguments
     * 
     * @param arguments Vector of arguments
     * @param option_name Argument to extract
     * @return std::string_view Returns the key to the argument in the vector if found
     */
    static std::string_view get_option(
            const std::vector<std::string_view>& arguments,
            const std::string_view& option_name);

    /**
     * Checks if the given path exists.
     * Function is required, as filesystem is not a valid C++ header.
     * @param path Path to check
     * @return Returns true if the given path is accessible, false otherwise.
     */
    static bool exists(const std::string& path);
};


#endif  // SRC_SPEAR_CLIHANDLER_H_
