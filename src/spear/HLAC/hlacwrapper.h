/*
 * Copyright (c) 2026 Maximilian Krebs
 * All rights reserved.
*/

#ifndef SRC_SPEAR_HLAC_HLACWRAPPER_H_
#define SRC_SPEAR_HLAC_HLACWRAPPER_H_

#include <memory>

#include "HLAC/hlac.h"

namespace HLAC {

class HLACWrapper {
 public:
    /**
     * Create a new HLAC graph
     * @return Returns unique pointer to the constructed graph
     */
    static std::unique_ptr<hlac> makeHLAC();
};

}  // namespace HLAC

#endif  // SRC_SPEAR_HLAC_HLACWRAPPER_H_
