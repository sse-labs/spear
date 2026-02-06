/*
 * Copyright (c) 2026 Maximilian Krebs
 * All rights reserved.
*/

#include "CLIOptions.h"
#include <utility>
#include <string>

ProfileOptions::ProfileOptions(std::string codePath, std::string configPath, std::string saveLocation) {
    this->codePath = std::move(codePath);
    this->configPath = std::move(configPath);
    this->saveLocation = std::move(saveLocation);

    this->operation = Operation::PROFILE;
}

AnalysisOptions::AnalysisOptions(std::string profilePath, std::string configPath, std::string programPath) {
    this->profilePath = std::move(profilePath);
    this->programPath = std::move(programPath);
    this->configPath = std::move(configPath);

    this->operation = Operation::ANALYZE;
}

CLIOptions::CLIOptions() {
    this->codePath = "";
    this->saveLocation = "";
    this->profilePath = "";
    this->operation = Operation::UNDEFINED;
    this->programPath = "";
}