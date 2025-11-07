
#include "RegisterReader.h"
#include <fcntl.h>
#include <unistd.h>
#include <cstdint>
#include "iostream"
#include "cstdio"
#include "cmath"


RegisterReader::RegisterReader(int core) {
    //Package -> 0x611
    //Cores -> 0x639
    this->energyReg = 0x639;
    this->unitReg = 0x606;
    sprintf(this->regFile, "/dev/cpu/%d/msr", core);
}

long long RegisterReader::read(int registerOffset) {
    int registerFileDescriptor = 0;
    uint64_t registerValueBuffer;

    //Open the file containing our registers in readonly mode
    registerFileDescriptor = open(this->regFile, O_RDONLY);
    //read 8 bytes from the registerfile at the offset energyReg and store the contents at the address of
    //our previously defined 64-integer regValBuffer
    pread(registerFileDescriptor, &registerValueBuffer, 8, registerOffset);

    close(registerFileDescriptor);

    return (long long) registerValueBuffer;
}

double RegisterReader::getEnergy() {
    u_int64_t result = read(this->energyReg);
    auto mutlitplier = this->readMultiplier();

    return (double) result * mutlitplier;
}

double RegisterReader::readMultiplier() {
    double unit;
    long long result = read(this->unitReg);
    unit = (char) ((result >> 8) & 0x1F);
    double multiplier = pow(0.5, (double) unit);
    return (double) multiplier;
}
