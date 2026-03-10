/*
 * Copyright (c) 2026 Maximilian Krebs
 * All rights reserved.
*/

#include <memory>

#include "HLAC/hlac.h"
#include "HLAC/hlacwrapper.h"

namespace HLAC {

std::unique_ptr<hlac> HLACWrapper::makeHLAC(ResultRegistry registry) {
    auto hlac = std::make_unique<HLAC::hlac>(registry);
    return hlac;
}

}  // namespace HLAC
