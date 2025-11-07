//
// Created by maximiliank on 18.01.23.
//

#ifndef BA_JSONHANDLER_H
#define BA_JSONHANDLER_H

#include <nlohmann/json.hpp>
using json = nlohmann::json;

/**
 * Class for handling the reading and writing of JSON files to save and load the energy data measured in the profile
 */
class JSONHandler {
    public:
        /**
         * Default constructor
         */
        explicit JSONHandler();

        /**
         * Method for writing the energy-data to a provided file
         * @param filename String containing the path to the file the method should write to
         * @param data Datastructure containing the energy values with respect to the specific group
         */
        static void write(const std::string& filename, const std::map<std::string, std::string>& cpu, const std::string& timeStartString, const std::string& timeEndString, const std::string&  iterationsString, const std::map<std::string, double>& data, const std::string& unit);

        /**
         * Method for reading the Json-Values from a provided file
         * @param filename Path to the file that should be read
         * @return Return the JSON::Value which was read from the provided file
         */
        static json read(const std::string& filename);
};


#endif //BA_JSONHANDLER_H
