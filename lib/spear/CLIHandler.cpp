/*
 * Copyright (c) 2026 Maximilian Krebs
 * All rights reserved.
*/


#include "CLIHandler.h"

#include <unistd.h>
#include <stdexcept>
#include <vector>
#include <string>

CLIOptions CLIHandler::parseCLI(int argc, char **argv) {
    // Pack the given parameters into a vector, so we can access them smartly
    const std::vector<std::string_view> arguments(argv + 1, argv + argc);

    // Init the options
    Operation operation = Operation::UNDEFINED;


    // Check the given arguments
    if (argc > 64) {
        throw std::runtime_error("Too many arguments");
    } else if (argc < 2) {
        throw std::runtime_error("Missing arguments");
    } else {
        // Check for the subprogram the user wants to execute
        for (const auto &arg : arguments) {
            if (arg == "analyze") {
                operation = Operation::ANALYZE;
            }

            if (arg == "profile") {
                operation = Operation::PROFILE;
            }
        }

        // Check the operations for the subprogram
        if (operation == Operation::PROFILE) {
            int repeat = -1;
            std::string modelPath;
            std::string savePath;
            std::string configPath;

            for (const auto &arg : arguments) {
                // Parse the model path
                if (arg == "--model") {
                    if (hasOption(arguments, "--model")) {
                        const std::string_view modelString = get_option(arguments, "--model");

                        if (CLIHandler::exists(modelString.data())) {
                            modelPath = modelString;
                        }
                    }
                }

                if (arg == "--config") {
                    if (hasOption(arguments, "--config")) {
                        const std::string_view configLocationString = get_option(arguments, "--config");

                        if (CLIHandler::exists(configLocationString.data())) {
                            configPath = configLocationString;
                        }
                    }
                }

                // Parse the save location given by the user
                if (arg == "--savelocation") {
                    if (hasOption(arguments, "--savelocation")) {
                        const std::string_view saveLocationString = get_option(arguments, "--savelocation");

                        if (CLIHandler::exists(saveLocationString.data())) {
                            savePath = saveLocationString;
                        }
                    }
                }
            }

            return ProfileOptions(modelPath, configPath, savePath);

        } else if (operation == Operation::ANALYZE) {
            std::string profilePath;
            std::string configPath;
            std::string programPath;
            std::string forFunction;

            for (const auto &arg : arguments) {
                if (arg == "--profile") {
                    if (hasOption(arguments, "--profile")) {
                        const std::string_view profileString = get_option(arguments, "--profile");

                        if (CLIHandler::exists(profileString.data())) {
                            profilePath = profileString;
                        }
                    }
                }

                if (arg == "--config") {
                    if (hasOption(arguments, "--config")) {
                        const std::string_view configLocationString = get_option(arguments, "--config");

                        if (CLIHandler::exists(configLocationString.data())) {
                            configPath = configLocationString;
                        }
                    }
                }

                if (arg == "--program") {
                    if (hasOption(arguments, "--program")) {
                        const std::string_view programString = get_option(arguments, "--program");

                        if (CLIHandler::exists(programString.data())) {
                            programPath = programString;
                        }
                    }
                }
            }
            return AnalysisOptions(profilePath, configPath, programPath);
        }
    }

    // Return an empty CLIOption Object
    return {};
}

bool CLIHandler::hasOption(const std::vector<std::string_view> &arguments, const std::string_view &option_name) {
    for (auto it = arguments.begin(), end = arguments.end(); it != end; ++it) {
        if (*it == option_name) {
            return true;
        }
    }

    return false;
}

std::string_view CLIHandler::get_option(
    const std::vector<std::string_view> &arguments,
    const std::string_view &option_name) {
    for (auto it = arguments.begin(), end = arguments.end(); it != end; ++it) {
        if (*it == option_name) {
            if (it + 1 != end) {
                return *(it + 1);
            }
        }
    }

    return "";
}

bool CLIHandler::exists(const std::string& path) {
    return ::access(path.c_str(), F_OK) == 0;
}
