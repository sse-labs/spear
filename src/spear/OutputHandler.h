
/*
 * Copyright (c) 2026 Maximilian Krebs
 * All rights reserved.
 */

#ifndef SRC_SPEAR_OUTPUTHANDLER_H_
#define SRC_SPEAR_OUTPUTHANDLER_H_

#include <string>
#include <unordered_map>

#include "nlohmann/json.hpp"

class OutputHandler {
 public:
    /**
     * Write the given content to a file with the given filename in JSON format.
     * @param filename Name of the file that will be written
     * @param content Content to write to the file
     */
    static void writeJsonOutput(std::string filename, nlohmann::json content);

    /**
     * Write the given content to a file with the given filename in the ELB format.
     * The content is expected to be a mapping of function names to energy values.
     * @param filename Name of the file that will be written
     * @param content Content to write to the file
     */
    static void writeELBOutput(std::string filename, std::unordered_map<std::string, double> content);

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
};

#endif  // SRC_SPEAR_OUTPUTHANDLER_H_
