/*
 * Copyright (c) 2026 Maximilian Krebs
 * All rights reserved.
*/

#ifndef SRC_SPEAR_REGISTERREADER_H_
#define SRC_SPEAR_REGISTERREADER_H_
#include <cstdint>


/**
 * Class to read out the Intel RAPL Registers
 */
class RegisterReader {
    /**
     * The address of the register containing the energycounter
     */
    int energyReg;
    /**
     * The address of the register containing the unit register
     */
    int unitReg;
    /**
     * Char-Array to safe the file containing all the processor register
     */
    char regFile[32]{};

 public:
        /**
         * Constructor setting the core to read the rapl registers from
         * @param core The core to read
         */
        explicit RegisterReader(int core);
        /**
         * Method to read the energy from the respective register
         * @return The current energy-counter
         */
        double getEnergy();
        /**
         * Method to read the energy-unit from the respective register
         * @return The current multiplier used for the energy-counter
         */
        double readMultiplier();

 private:
        /**
         * Reads a register in the register-file of the class
         * @param registerOffset Offset of the register in the file
         * @return Value in the register as 64-bit unsigned integer
         */
        int64_t read(int registerOffset);
};


#endif  // SRC_SPEAR_REGISTERREADER_H_
