
/*
 * Copyright (c) 2026 Maximilian Krebs
 * All rights reserved.
 */

#ifndef SPEAR_MONOLITHICANALYSIS_H
#define SPEAR_MONOLITHICANALYSIS_H
#include "HLAC/hlac.h"
#include "nlohmann/json.hpp"

class MonolithicAnalysis {
 public:

    static nlohmann::json run(std::shared_ptr<HLAC::hlac> graph, bool showTimings);
};

#endif //SPEAR_MONOLITHICANALYSIS_H
