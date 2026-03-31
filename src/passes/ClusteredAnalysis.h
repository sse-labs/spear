
/*
 * Copyright (c) 2026 Maximilian Krebs
 * All rights reserved.
 */

#ifndef SPEAR_CLUSTEREDANALYSIS_H
#define SPEAR_CLUSTEREDANALYSIS_H
#include "HLAC/hlac.h"
#include "nlohmann/json.hpp"

class ClusteredAnalysis {
public:

    static nlohmann::json run(std::shared_ptr<HLAC::hlac> graph, bool showTimings);
};

#endif //SPEAR_CLUSTEREDANALYSIS_H
