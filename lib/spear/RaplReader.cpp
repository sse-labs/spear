
#include "Domain.h"
#include "RAPLReader.h"
#include <fcntl.h>
#include <unistd.h>
#include <cstdint>
#include "iostream"
#include "cmath"

uint16_t RaplReader::unitRegister = 0x606;

double RaplReader::readEnergy(Domain domain) {
    u_int64_t result = readRawEnergy(domain);
    return convertRawValueToEnergy(result);
}

long long RaplReader::readRawEnergy(Domain domain) {
    return _readRegister(domain.address);
}

double RaplReader::convertRawValueToEnergy(long long rawValue) {
    auto mutlitplier = _readMultiplier();

    return static_cast<double>(rawValue) * mutlitplier;
}

double RaplReader::_readMultiplier() {
    double unit = 0.0;
    long long result = _readRegister(RaplReader::unitRegister);
    unit = static_cast<char>((result >> 8) & 0x1F);
    double multiplier = pow(0.5, unit);
    return multiplier;
}

long long RaplReader::_readRegister(int registerOffset) {
    int registerFileDescriptor = 0;
    uint64_t registerValueBuffer;

    //Open the file containing our registers in readonly mode
    registerFileDescriptor = open("/dev/cpu/0/msr", O_RDONLY);
    //read 8 bytes from the registerfile at the offset energyReg and store the contents at the address of
    //our previously defined 64-integer regValBuffer
    pread(registerFileDescriptor, &registerValueBuffer, 8, registerOffset);

    close(registerFileDescriptor);

    return (long long) registerValueBuffer;
}

