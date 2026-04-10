/*
 * Copyright (c) 2026 Maximilian Krebs
 * All rights reserved.
 */

#include "ELBs/ELBParser.h"
#include "ELBs/ELPMapper.h"

ELBMapper &ELBMapper::getInstance() {
    static ELBMapper instance;
    return instance;
}

void ELBMapper::useMapping(const std::string &filename) {
    auto parsedMapping = ELBParser::parseELBFile(filename);

    for (const auto& [key, value] : parsedMapping) {
        this->mapping[key] = value;
    }
}

std::unordered_map<std::string, double> ELBMapper::getMapping() {
    return this->mapping;
}

std::optional<double> ELBMapper::lookup(std::string fname) {
    if (this->mapping.empty() || this->mapping.find(fname) == this->mapping.end()) {
        return std::nullopt;
    }

    return this->mapping[fname];
}
