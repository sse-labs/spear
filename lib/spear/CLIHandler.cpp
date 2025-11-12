#include "CLIHandler.h"

CLIOptions CLIHandler::parseCLI(int argc, char **argv) {
    // Pack the given parameters into a vector, so we can access them smartly
    const std::vector<std::string_view> arguments(argv + 1, argv + argc);

    // Init the options
    Operation operation = Operation::UNDEFINED;


    // Check the given arguments
    if(argc > 64){
        throw std::runtime_error("Too many arguments");
    }else if(argc < 2){
        throw std::runtime_error("Missing arguments");
    }else{
        // Check for the subprogram the user wants to execute
        for(const auto &arg : arguments){
            if(arg == "analyze"){
                operation = Operation::ANALYZE;
            }

            if(arg == "profile"){
                operation = Operation::PROFILE;
            }
        }

        // Check the operations for the subprogram
        if(operation == Operation::PROFILE){
            int repeat = -1;
            std::string modelPath;
            std::string savePath;

            for(const auto &arg : arguments){
                // Parse the iterations given by the user
                if(arg == "--iterations"){
                    if(hasOption(arguments, "--iterations")){
                        const std::string_view iterationsString = get_option(arguments, "--iterations");

                        try{
                            repeat = std::stoi(iterationsString.data());
                        }catch(std::exception &E){
                            repeat = -1;
                        }
                    }
                }

                // Parse the model path
                if(arg == "--model"){
                    if(hasOption(arguments, "--model")){
                        const std::string_view modelString = get_option(arguments, "--model");

                        if(std::filesystem::exists(modelString)){
                            modelPath = modelString;
                        }
                    }
                }

                // Parse the save location given by the user
                if(arg == "--savelocation"){
                    if(hasOption(arguments, "--savelocation")){
                        const std::string_view saveLocationString = get_option(arguments, "--savelocation");

                        if(std::filesystem::exists(saveLocationString)){
                            savePath = saveLocationString;
                        }
                    }
                }
            }

            return ProfileOptions(modelPath, repeat, savePath);

        }else if(operation == Operation::ANALYZE){
            std::string profilePath;
            Mode mode = Mode::UNDEFINED;
            Format format = Format::UNDEFINED;
            DeepCalls deepCalls = DeepCalls::UNDEFINED;
            Strategy strategy = Strategy::UNDEFINED;
            int loopBound = -1;
            std::string programPath;
            std::string forFunction;

            for(const auto &arg : arguments){
                if(arg == "--profile"){
                    if(hasOption(arguments, "--profile")){
                        const std::string_view profileString = get_option(arguments, "--profile");

                        if(std::filesystem::exists(profileString)){
                            profilePath = profileString;
                        }
                    }
                }

                if(arg == "--mode"){
                    if(hasOption(arguments, "--mode")){
                        const std::string_view modeString = get_option(arguments, "--mode");

                        if(modeString == "program"){
                            mode = Mode::PROGRAM;
                        }else if(modeString == "function"){
                            mode = Mode::FUNCTION;
                        }else if(modeString == "block"){
                            mode = Mode::BLOCK;
                        }else if(modeString == "instruction"){
                            mode = Mode::INSTRUCTION;
                        }else if(modeString == "graph"){
                            mode = Mode::GRAPH;
                        }
                    }
                }

                if(arg == "--format"){
                    if(hasOption(arguments, "--format")){
                        const std::string_view formatString = get_option(arguments, "--format");

                        if(formatString == "plain"){
                            format = Format::PLAIN;
                        }else if(formatString == "json"){
                            format = Format::JSON;
                        }
                    }
                }

                if(arg == "--strategy"){
                    if(hasOption(arguments, "--strategy")){
                        const std::string_view strategyString = get_option(arguments, "--strategy");

                        if(strategyString == "worst"){
                            strategy = Strategy::WORST;
                        }else if(strategyString == "average"){
                            strategy = Strategy::AVERAGE;
                        }else if(strategyString == "best"){
                            strategy = Strategy::BEST;
                        }
                    }
                }

                if(arg == "--withCalls"){
                    deepCalls = DeepCalls::ENABLED;
                }

                if(arg == "--loopbound"){
                    if(hasOption(arguments, "--loopbound")){
                        const std::string_view loopboundString = get_option(arguments, "--loopbound");

                        try{
                            loopBound = std::stoi(loopboundString.data());
                        }catch(std::exception &E){
                            loopBound = -1;
                        }
                    }
                }

                if(arg == "--program"){
                    if(hasOption(arguments, "--program")){
                        const std::string_view programString = get_option(arguments, "--program");

                        if(std::filesystem::exists(programString)){
                            programPath = programString;
                        }
                    }
                }

                if(arg == "--forFunction"){
                    if(hasOption(arguments, "--forFunction")){
                        const std::string_view functionName = get_option(arguments, "--forFunction");
                        forFunction = functionName;
                    }
                }

            }

            return AnalysisOptions(profilePath, mode, format, strategy, loopBound, programPath, deepCalls, forFunction);
        }

    }

    // Return an empty CLIOption Object
    return {};
}

bool CLIHandler::hasOption(const std::vector<std::string_view> &arguments, const std::string_view &option_name) {
    for (auto it = arguments.begin(), end = arguments.end(); it != end; ++it) {
        if (*it == option_name){
            return true;
        }
    }

    return false;
}

std::string_view CLIHandler::get_option(const std::vector<std::string_view> &arguments, const std::string_view &option_name) {
    for (auto it = arguments.begin(), end = arguments.end(); it != end; ++it) {
        if (*it == option_name){
            if (it + 1 != end){
                return *(it + 1);
            }
        }

    }

    return "";
}
