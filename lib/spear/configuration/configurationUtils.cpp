/*
 * Copyright (c) 2026 Maximilian Krebs
 * All rights reserved.
 */

#include <string>
#include "configuration/configurationUtils.h"


Mode ConfigurationUtils::strToMode(const std::string& str) {
    if (str == "program") {
        return Mode::PROGRAM;
    } else if (str == "function") {
        return Mode::FUNCTION;
    } else if (str == "instruction") {
        return Mode::INSTRUCTION;
    } else if (str == "block") {
        return Mode::BLOCK;
    } else {
        return Mode::UNDEFINED;
    }
}

Strategy ConfigurationUtils::strToStrategy(const std::string &str) {
    if (str == "worst") {
        return Strategy::WORST;
    } else if (str == "average") {
        return Strategy::AVERAGE;
    } else if (str == "worst") {
        return Strategy::WORST;
    } else {
        return Strategy::UNDEFINED;
    }
}

Format ConfigurationUtils::strToFormat(const std::string &str) {
    if (str == "plain") {
        return Format::PLAIN;
    } else if (str == "json") {
        return Format::JSON;
    } else {
        return Format::UNDEFINED;
    }
}

AnalysisType ConfigurationUtils::strToAnalysisType(const std::string& str) {
    if (str == "monolithic") {
        return AnalysisType::MONOLITHIC;
    } else if (str == "clustered") {
        return AnalysisType::CLUSTERED;
    } else if (str == "legacy") {
        return AnalysisType::LEGACY;
    } else if (str == "comparison") {
        return AnalysisType::COMPARISON;
    } else {
        return AnalysisType::UNDEFINED;
    }
}

AnalysisOutputMode ConfigurationUtils::strToAnalysisOutputmode(const std::string& str) {
    if (str == "normal") {
        return AnalysisOutputMode::NORMAL;
    } else if (str == "elb") {
        return AnalysisOutputMode::ELB;
    } else {
        return AnalysisOutputMode::UNDEFINED;
    }
}
