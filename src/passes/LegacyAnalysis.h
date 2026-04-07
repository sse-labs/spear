
/*
 * Copyright (c) 2026 Maximilian Krebs
 * All rights reserved.
 */

#ifndef SPEAR_LEGACYANALYSIS_H
#define SPEAR_LEGACYANALYSIS_H
#include "FunctionTree.h"
#include "HLAC/hlac.h"
#include "ProgramGraph.h"
#include "nlohmann/json.hpp"

class LegacyAnalysis {
public:

    static nlohmann::json run(
        llvm::FunctionAnalysisManager &FAM,
        FunctionTree *functionTree,
        bool showTimings, bool showAllTiming = false);

 private:
    static void constructProgramRepresentation(ProgramGraph *pGraph, EnergyFunction *energyFunc, LLVMHandler *handler,
                                               llvm::FunctionAnalysisManager *FAM,
                                               AnalysisStrategy::Strategy analysisStrategy);
};

#endif //SPEAR_LEGACYANALYSIS_H
