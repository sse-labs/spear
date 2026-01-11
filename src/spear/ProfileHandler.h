/*
 * Copyright (c) 2026 Maximilian Krebs
 * All rights reserved.
*/

#ifndef SRC_SPEAR_PROFILEHANDLER_H_
#define SRC_SPEAR_PROFILEHANDLER_H_

#include <map>
#include <string>
#include <variant>
#include <nlohmann/json.hpp>
using json = nlohmann::json;

using profileMap = std::variant<
    std::map<std::string, double>,
    std::map<std::string, std::string>
>;

/**
 * Class for handling the reading and writing of profiles
 */
class ProfileHandler {
 public:
    /**
     * Default constructor
     */
    explicit ProfileHandler();

    /**
     * Method for writing the profile data stored inside the handler
     * @param filename String containing the path to the file the method should write to
     */
    void write(const std::string& filename);

    /**
     * Method for reading the Json-Values from a provided file.
     * Reads the data to the local _profile object
     * @param filename Path to the file that should be read
     */
    void read(const std::string& filename);

    /**
     * Query the local _profile variable
     * @return The profile as JSON object
     */
    json getProfile();

    /**
     * Write a json object to a key in the internal _profile object.
     * If the entry found under the specific key cannot be found, it will be created
     * Updated otherwise.
     * @param key Key the entry will be saved at
     * @param mapping JSON object to be saved
     */
    void setOrCreate(std::string key, json &mapping);

 private:
    /**
     * Internal profile storage as JSON object
     */
    json _profile;
};


#endif  // SRC_SPEAR_PROFILEHANDLER_H_
