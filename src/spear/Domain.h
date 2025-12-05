#ifndef SPEAR_DOMAIN_H
#define SPEAR_DOMAIN_H
#include <cstdint>

class Domain {
public:
    int address;
};

constexpr Domain CPU_DOMAIN{0x639};
constexpr Domain DRAM_DOMAIN{0x619};

#endif //SPEAR_DOMAIN_H