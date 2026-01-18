//
// Created by max on 1/16/26.
//

#include <memory>

#include "HLAC/hlac.h"
#include "HLAC/hlacwrapper.h"


std::unique_ptr<HLAC::hlac> HLAC::HLACWrapper::makeHLAC() {
    auto hlac = std::make_unique<HLAC::hlac>();

    return hlac;
}
