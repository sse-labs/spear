
/*
 * Copyright (c) 2026 Maximilian Krebs
 * All rights reserved.
 */

#ifndef SRC_SPEAR_ELBS_ELPMAPPER_H_
#define SRC_SPEAR_ELBS_ELPMAPPER_H_

#include <optional>
#include <string>
#include <unordered_map>

using ELBMapping = std::unordered_map<std::string, double>;

/**
 * ELBMapper class.
 * Manages parsed ELBMappings internally and enables lookup through the respective methods
 */
class ELBMapper {
 public:
    /**
     * Get Singleton instance of the class
     * @return ELBMapper instance
     */
    static ELBMapper &getInstance();

    /**
     * Load the mapping from the given file.
     * ELB-Functions have to be unique. The function simply overrides ELB-Values if encountering duplicate values
     * @param filename Name of the file to parse the mapping from
     */
    void useMapping(const std::string &filename);

    /**
     * Get ELBValue for a given function name from the internal mapping if it exists
     * @param fname Name of the function to look up
     * @return Nullopt if the function is not found in the mapping, the saved double value otherwise
     */
    std::optional<double> lookup(std::string fname);

    /**
     * Return the full mapping
     * @return Mapping stored in the mapper
     */
    ELBMapping getMapping();

 private:
    /**
     * Singleton instance modeled by this class
     */
    static ELBMapper *instance;

    /**
     * Internal mapping that mapps function names as string to double energy values
     */
    ELBMapping mapping;

    /**
     * Private constructor to prevent direct instantiation
     */
    ELBMapper() = default;

    /**
     * Deleted copy constructor and assignment operator
     */
    ELBMapper(const ELBMapper &) = delete;
    ELBMapper &operator=(const ELBMapper &) = delete;
};

#endif  // SRC_SPEAR_ELBS_ELPMAPPER_H_
