
/*
 * Copyright (c) 2026 Maximilian Krebs
 * All rights reserved.
 */

#ifndef SRC_SPEAR_ELBS_ELBPARSER_H_
#define SRC_SPEAR_ELBS_ELBPARSER_H_

#include <string>
#include <unordered_map>

using ELBMapping = std::unordered_map<std::string, double>;

/**
 * ELBParser class.
 * Parses ELB files and returns the stored content
 */
class ELBParser {
 public:
    /**
     * Parse the ELBFile under the given path if possible
     * @param filepath Path to parse the ELB file from
     * @return Parsed ELBMapping. An empty mapping if an error occours
     */
    static ELBMapping parseELBFile(const std::string &filepath);
};

#endif  // SRC_SPEAR_ELBS_ELBPARSER_H_
