/*
 * Copyright (c) 2026 Maximilian Krebs
 * All rights reserved.
 */

#ifndef SRC_SPEAR_ANALYSES_FEASIBILITY_FEASIBILITYWRAPPER_H_
#define SRC_SPEAR_ANALYSES_FEASIBILITY_FEASIBILITYWRAPPER_H_

#include <llvm/IR/PassManager.h>
#include <phasar/PhasarLLVM/HelperAnalyses.h>
#include <memory>
#include <phasar/DataFlow/IfdsIde/SolverResults.h>

#include "FeasibilityAnalysis.h"

namespace Feasibility {

/**
 * Resulttype of the analysis, which is a mapping from instructions to their corresponding feasibility elements.
 */
using ResultsTy = psr::OwningSolverResults<
    const llvm::Instruction *,
    const llvm::Value *,
    Feasibility::FeasibilityElement>;

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

    /**
     * Return the resulting formular per instruction information.
     * @return A unique pointer to the results of the analysis,
     * which is a mapping from instructions to their corresponding feasibility elements.
     */
    [[nodiscard]] std::unique_ptr<ResultsTy> getResults() const;

    /**
     * Internal shared pointer to the used analysis problem instance
     */
    std::shared_ptr<Feasibility::FeasibilityAnalysis> problem;

private:
    // Internal storage of the analysis results calculated by phasar
    std::unique_ptr<ResultsTy> cachedResults;

    // Internal storage of the analysis manager so we can access llvms analysis information later on without
    // passing it down to our functions
    llvm::FunctionAnalysisManager *FAM = nullptr;
};

}

#endif  // SRC_SPEAR_ANALYSES_FEASIBILITY_FEASIBILITYWRAPPER_H_
