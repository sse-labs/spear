
#ifndef SPEAR_RAPLREADER_H
#define SPEAR_RAPLREADER_H

#include "Domain.h"

class RaplReader {
public:
    RaplReader() = delete;

    /**
     * Read the energy from the given domain
     * @param domain Domain do read from
     * @return energy as double value
     */
    static double readEnergy(Domain domain);

    static long long readRawEnergy(Domain domain);

    static double convertRawValueToEnergy(long long rawValue);

    ~RaplReader() = delete;

private:
    static uint16_t unitRegister;

    /**
     * Reads a register in the register-file of the class
     * @param registerOffset Offset of the register in the file
     * @return Value in the register as 64-bit unsigned integer
     */
    static long long _readRegister(int registerOffset);

    /**
     * Method to read the energy-unit from the respective register
     * @return The current multiplier used for the energy-counter
     */
    static double _readMultiplier();
};

#endif //SPEAR_RAPLREADER_H