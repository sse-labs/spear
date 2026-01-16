//
// Created by max on 1/16/26.
//

#include "HLAC/hlac.h"
#include "HLAC/hlacwrapper.h"


std::unique_ptr<HLAC::hlac> HLAC::HLACWrapper::makeHLAC() {
    auto hlac = std::unique_ptr<HLAC::hlac>(new HLAC::hlac());

    return hlac;
}
