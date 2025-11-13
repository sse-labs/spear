#include <fstream>
#include "iostream"
#include "ProfileHandler.h"

ProfileHandler::ProfileHandler() = default;

//Read the data from the provided file
void ProfileHandler::read(const std::string& filename) {
    //init reader and data
    json data;

    //Create a filestream to the provided file
    std::ifstream fileStream(filename);

    //Move the data from the stream to our initialized JSON::Value object
    data = json::parse(fileStream);

    std::cout << data << "\n";

    _profile = data;
}

void ProfileHandler::setOrCreate(std::string key, json &mapping) {
    _profile[key]= mapping;
}

json ProfileHandler::getProfile() {
    return _profile;
}

void ProfileHandler::write(const std::string& filename) {
    // Create a filestream and open it
    std::ofstream fileStream;
    fileStream.open(filename);

    if(fileStream.is_open()) {
        //Use the json dump method to write the data to the filestream
        fileStream << _profile.dump(4);
        fileStream.close();
    }else{
        std::cout << "ERROR opening the file" << "\n";
    }
}
