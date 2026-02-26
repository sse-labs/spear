/*
 * Copyright (c) 2026 Maximilian Krebs
 * All rights reserved.
 */

#include <phasar/DataFlow/IfdsIde/Solver/IDESolver.h>
#include <phasar/DataFlow/IfdsIde/IDETabulationProblem.h>
#include <phasar/PhasarLLVM/DB/LLVMProjectIRDB.h>
#include <llvm/Support/raw_ostream.h>

#include <utility>
#include <memory>

#include "analyses/feasibility/FeasibilityWrapper.h"
#include "analyses/feasibility/FeasibilityAnalysis.h"
#include "analyses/feasibility/util.h"

Feasibility::FeasibilityWrapper::FeasibilityWrapper(std::shared_ptr<psr::HelperAnalyses> helperAnalyses,
                                                    llvm::FunctionAnalysisManager *analysisManager) {
    // Make sure that the helper analyses are available.
    if (!helperAnalyses) {
        return;
    }

    this->FAM = analysisManager;

    // Get the module under analysis
    llvm::Module *module = helperAnalyses->getProjectIRDB().getModule();
    if (!module) {
        llvm::errs() << Util::debugtag << "module not found\n";
        return;
    }

    // Get the IRDB and ICFG from the helper analyses, which are needed to set up the analysis problem.
    auto &IRDB = helperAnalyses->getProjectIRDB();
    // Interprocedural CFG
    auto &interproceduralCFG = helperAnalyses->getICFG();

    // Create a new instance of the feasibility analysis problem, which will be solved by the IDE solver.
    this->problem = std::make_shared<FeasibilityAnalysis>(
        FeasibilityAnalysis(analysisManager, &IRDB, &interproceduralCFG));

    if (Util::F_DebugEnabled) {
        llvm::errs() << Util::debugtag << " Starting IDESolver.solve()\n";
    }

    // Solve the analysis problem using the IDE solver and store the results in the cachedResults member variable.
    auto analysisResult = psr::solveIDEProblem(*this->problem.get(), interproceduralCFG);
    // Query the result of the analysis
    this->cachedResults = std::make_unique<ResultsTy>(std::move(analysisResult));

    if (Util::F_DebugEnabled) {
        llvm::errs() << Util::debugtag << " Finished IDESolver.solve()\n";
    }
}

std::unique_ptr<Feasibility::ResultsTy> Feasibility::FeasibilityWrapper::getResults() const {
    return std::make_unique<ResultsTy>(*this->cachedResults);
}
