/*
 * Copyright (c) 2026 Maximilian Krebs
 * All rights reserved.
 */

#ifndef SRC_SPEAR_ANALYSES_FEASIBILITY_FEASIBILITYWRAPPER_H_
#define SRC_SPEAR_ANALYSES_FEASIBILITY_FEASIBILITYWRAPPER_H_

#include <llvm/IR/PassManager.h>
#include <phasar/PhasarLLVM/HelperAnalyses.h>
#include <memory>

#include "analyses/loopbound/loopBoundWrapper.h"

/**
 * FeasibilityWrapper class
 *
 * Given program information this wrapper calculates feasibility for the program and stores the results for later
 * querying.
 */
class FeasibilityWrapper {
 public:
    /**
     * FeasibiltyWrapper constructor. That creates a new wrapper that executes the feasibility analysis
     * and stores the results for later querying.
     * @param helperAnalyses Phasar helper analyses to access the IR and other analysis results
     * @param analysisManager LLVM's analysis manager to access LLVM's analysis results
     */
    FeasibilityWrapper(std::shared_ptr<psr::HelperAnalyses> helperAnalyses,
                       llvm::FunctionAnalysisManager *analysisManager);

 private:
    // Internal storage of the analysis manager so we can access llvms analysis information later on without
    // passing it down to our functions
    llvm::FunctionAnalysisManager *FAM = nullptr;
};

#endif  // SRC_SPEAR_ANALYSES_FEASIBILITY_FEASIBILITYWRAPPER_H_
