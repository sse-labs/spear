//
// Created by max on 1/16/26.
//

#ifndef SPEAR_HLACWRAPPER_H
#define SPEAR_HLACWRAPPER_H

namespace HLAC {

#include "hlac.h"

class HLACWrapper {
public:
    static std::unique_ptr<HLAC::hlac> makeHLAC();
};

}

#endif //SPEAR_HLACWRAPPER_H