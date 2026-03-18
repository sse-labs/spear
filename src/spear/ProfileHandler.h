/*
* Copyright (c) 2026 Maximilian Krebs
 * All rights reserved.
*/

#ifndef SRC_SPEAR_PROFILEHANDLER_H_
#define SRC_SPEAR_PROFILEHANDLER_H_

#include <map>
#include <string>
#include <variant>
#include <optional>
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
     * Retrieve the global singleton instance
     */
    static ProfileHandler& get_instance();

    /**
     * Delete copy semantics
     */
    ProfileHandler(const ProfileHandler&) = delete;
    ProfileHandler& operator=(const ProfileHandler&) = delete;

    /**
     * Delete move semantics
     */
    ProfileHandler(ProfileHandler&&) = delete;
    ProfileHandler& operator=(ProfileHandler&&) = delete;

    /**
     * Method for writing the profile data stored inside the handler
     */
    void write(const std::string& filename);

    /**
     * Return the energy of the instruction specified by the given string
     * @param instruction Instruction string to receive energy for
     * @return Energy if the instruction exists in the profile, nullopt otherwise
     */
    std::optional<double> getEnergyForInstruction(const std::string &instruction);

    /**
     * Return the program offset value from the profile, if it exists
     * @return Program offset energy if entry exits in the profile, nullopt otherwise
     */
    std::optional<double> getProgramOffset();

    /**
     * Return the fallback cost value from the profile, if it exists
     * @return Fallback energy cost if entry exits in the profile, nullopt otherwise
     */
    std::optional<double> getUnknownCost();

    /**
     * Return the energy of the syscall found under the given name
     * @param syscall Name of the syscall to retrieve energy for
     * @return Energy if the syscall can be found in the profile, nullopt otherwise
     */
    std::optional<double> getEnergyForSyscall(const std::string &syscall);

    /**
     * Method for reading the Json-Values from a provided file.
     */
    void read(const std::string& filename);

    /**
     * Query the local _profile variable
     */
    json getProfile();

    /**
     * Write a json object to a key in the internal _profile object.
     */
    void setOrCreate(std::string key, json &mapping);

 private:
    /**
     * Private constructor (singleton)
     */
    ProfileHandler();

    /**
     * Internal profile storage
     */
    json _profile;
};

#endif  // SRC_SPEAR_PROFILEHANDLER_H_
