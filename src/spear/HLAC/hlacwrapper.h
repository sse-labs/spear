/*
 * Copyright (c) 2026 Maximilian Krebs
 * All rights reserved.
*/

#ifndef SRC_SPEAR_HLAC_HLACWRAPPER_H_
#define SRC_SPEAR_HLAC_HLACWRAPPER_H_

#include <memory>

namespace HLAC {

class HLACWrapper {
 public:
    static std::unique_ptr<hlac> makeHLAC();
};

}

#endif  // SRC_SPEAR_HLAC_HLACWRAPPER_H_
