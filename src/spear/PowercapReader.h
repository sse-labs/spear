#ifndef BA_POWERCAPREADER_H
#define BA_POWERCAPREADER_H


#include <cstdint>
#include "string"

/**
 * Class to read out the Intel RAPL Registers
 */
class PowercapReader {
    /**
     * The path of the rapl-powercap interface
     */
    std::string basePath;

public:
    /**
     * Constructor creating the reader
     */
    explicit PowercapReader();
    /**
     * Method to read the energy from the interface
     * @return The current energy-counter
     */
    uint64_t getEnergy();
private:
    /**
     * Reads the provided rapl file and returns the value
     * @param file A '/' followed by the path to the file that should be read
     * @return Value in the file as 64-bit unsigned integer
     */
    uint64_t read(std::string file);
};


#endif //BA_POWERCAPREADER_H
