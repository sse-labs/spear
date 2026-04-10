
/*
 * Copyright (c) 2026 Maximilian Krebs
 * All rights reserved.
 */

#ifndef SPEAR_ELBPARSER_H
#define SPEAR_ELBPARSER_H

#include <string>
#include <unordered_map>

class ELBParser {
 public:
    static std::unordered_map<std::string, double> parseELBFile(const std::string &filename);
};

#endif  // SPEAR_ELBPARSER_H
