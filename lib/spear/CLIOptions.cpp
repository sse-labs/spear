#include "CLIOptions.h"

#include <utility>

ProfileOptions::ProfileOptions(std::string codePath, int repeatAmount, std::string saveLocation){
    this->codePath = std::move(codePath);
    this->repeatAmount = repeatAmount;
    this->saveLocation = std::move(saveLocation);

    this->operation = Operation::PROFILE;
}

AnalysisOptions::AnalysisOptions(std::string profilePath, Mode mode, Format format, Strategy strategy, int loopBound,
                                 std::string programPath, DeepCalls deepCalls, std::string forFunction) {
    this->profilePath = std::move(profilePath);
    this->mode = mode;
    this-> format = format;
    this->strategy = strategy;
    this->loopBound = loopBound;
    this->deepCalls = deepCalls;
    this->programPath = std::move(programPath);
    this->forFunction = std::move(forFunction);

    this->operation = Operation::ANALYZE;
}

CLIOptions::CLIOptions() {
    this->codePath = "";
    this->repeatAmount = -1;
    this->saveLocation = "";
    this->profilePath = "";
    this->operation = Operation::UNDEFINED;
    this->mode = Mode::UNDEFINED;
    this-> format = Format::UNDEFINED;
    this->strategy = Strategy::UNDEFINED;
    this->loopBound = -1;
    this->programPath = "";
}

Mode CLIOptions::strToMode(const std::string& str) {
    if(str == "program"){
        return Mode::PROGRAM;
    }else if(str == "function"){
        return Mode::FUNCTION;
    }else if(str == "instruction"){
        return Mode::INSTRUCTION;
    }else if(str == "block"){
        return Mode::BLOCK;
    }else{
        return Mode::UNDEFINED;
    }
}

Strategy CLIOptions::strToStrategy(const std::string &str) {
    if(str == "worst"){
        return Strategy::WORST;
    }else if(str == "average"){
        return Strategy::AVERAGE;
    }else if(str == "worst"){
        return Strategy::WORST;
    }else{
        return Strategy::UNDEFINED;
    }
}

Format CLIOptions::strToFormat(const std::string &str) {
    if(str == "plain"){
        return Format::PLAIN;
    }else if(str == "json"){
        return Format::JSON;
    }else{
        return Format::UNDEFINED;
    }
}
