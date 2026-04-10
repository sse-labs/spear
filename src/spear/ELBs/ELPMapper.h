
/*
 * Copyright (c) 2026 Maximilian Krebs
 * All rights reserved.
 */

#ifndef SPEAR_ELPMAPPER_H
#define SPEAR_ELPMAPPER_H

#include <optional>
#include <stdexcept>
#include <string>
#include <unordered_map>

class ELBMapper {
 public:
    // Get singleton instance
    static ELBMapper &getInstance();

    // Use mapping with filename
    void useMapping(const std::string &filename);

    std::optional<double> lookup(std::string fname);

    std::unordered_map<std::string, double> getMapping();

 private:
    // Singleton instance
    static ELBMapper *instance;

    // Internal mapping: string -> double
    std::unordered_map<std::string, double> mapping;

    // Private constructor to prevent direct instantiation
    ELBMapper() = default;

    // Delete copy constructor and assignment operator
    ELBMapper(const ELBMapper &) = delete;
    ELBMapper &operator=(const ELBMapper &) = delete;
};

#endif  // SPEAR_ELPMAPPER_H
