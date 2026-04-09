
/*
 * Copyright (c) 2026 Maximilian Krebs
 * All rights reserved.
 */

#ifndef SPEAR_OUTPUTHANDLER_H
#define SPEAR_OUTPUTHANDLER_H
#include <string>

#include "configuration/valuespace.h"
#include "nlohmann/json.hpp"

class OutputHandler {
 public:

    static void writeJsonOutput(std::string filename, nlohmann::json content, bool writeMultiple);

    static void writeELBOutput(std::string filename, std::unordered_map<std::string, double> content, bool writeMultiple);

 private:
    /**
     * Create a json file under the given filename and fill it with the given content
     * @param filename Name of the file
     * @param content Content to write to the file
     */
    static void writeJsonFile(std::string filename, nlohmann::json content);

    /**
     * Create a ELB-File under the given filename and write the function to energy mapping to the file
     * @param filename Name of the ELB-File
     * @param content Mapping to write to
     */
    static void writeELBFile(std::string filename, nlohmann::json content);

    /**
     *
     * @param mode
     * @return
     */
    static std::string getFileNameFromAnalysisType(AnalysisType type);
};

#endif  // SPEAR_OUTPUTHANDLER_H
