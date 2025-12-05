#ifndef SPEAR_DOMAIN_H
#define SPEAR_DOMAIN_H
#include <cstdint>

class Domain {
public:
    uint16_t address;
};

constexpr Domain CPU_DOMAIN{0x639};
constexpr Domain DRAM_DOMAIN{0x640};

#endif //SPEAR_DOMAIN_H