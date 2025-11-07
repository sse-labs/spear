//
// Created by max on 27.12.22.
//

#include "PowercapReader.h"

#include <fstream>
#include "iostream"



PowercapReader::PowercapReader() {
    this->basePath = "/sys/class/powercap/intel-rapl:0:0";
}

uint64_t PowercapReader::read(std::string file) {
    std::ifstream energyFile;
    std::string energyStr;
    std::string buffer;

    std::string path = this->basePath;

    try{
        energyFile.open(path.append(file));
    }catch(std::ios_base::failure& e){
        std::cerr << e.what() << "\n";
    }

    if(energyFile.is_open()){
        while(!energyFile.eof()){
            getline(energyFile, buffer);
            energyStr.append(buffer);
        }
    }
    energyFile.close();

    long energy = stol(energyStr);

    return energy;
}

uint64_t PowercapReader::getEnergy() {
    return read("/energy_uj");
}
