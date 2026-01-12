/*
 * Copyright (c) 2026 Maximilian Krebs
 * All rights reserved.
*/

#include "RegisterReader.h"
#include <fcntl.h>
#include <unistd.h>
#include <cstdint>
#include "iostream"
#include "cstdio"
#include "cmath"

RegisterReader::RegisterReader(int core) {
    // Package -> 0x611
    // Cores -> 0x639
#if CPU_VENDOR_INTEL
    this->energyReg = 0x639;
    this->unitReg = 0x606;
#elif CPU_VENDOR_AMD
    this->energyReg = 0xC001029A;
    this->unitReg = 0xC0010299;
#else
    std::cerr << "UNKNOWN CPU DETECTED"
    << "\nAborting profiling..."
    << std::endl;
    this->energyReg = 0;
    this->unitReg = 0;
    throw std::runtime_error("Unknown CPU detected");
#endif
    snprintf(this->regFile, sizeof(this->regFile), "/dev/cpu/%d/msr", core);
}

int64_t RegisterReader::read(int registerOffset) {
    int registerFileDescriptor = 0;
    uint64_t registerValueBuffer;

    // Open the file containing our registers in readonly mode
    registerFileDescriptor = open(this->regFile, O_RDONLY);
    // read 8 bytes from the registerfile at the offset energyReg and store the contents at the address of
    // our previously defined 64-integer regValBuffer
    pread(registerFileDescriptor, &registerValueBuffer, 8, registerOffset);

    close(registerFileDescriptor);

    return static_cast<int64_t>(registerValueBuffer);
}

double RegisterReader::getEnergy() {
    u_int64_t result = read(this->energyReg);
    auto mutlitplier = this->readMultiplier();

    return result * mutlitplier;
}

double RegisterReader::readMultiplier() {
    uint64_t result = read(this->unitReg);
    double unit = static_cast<char>(((result >> 8) & 0x1F));
    double multiplier = pow(0.5, unit);
    return multiplier;
}
