/*
 * Copyright (c) 2026 Maximilian Krebs
 * All rights reserved.
 */

#include "analyses/feasibility/FeasibilityWrapper.h"

#include <phasar/DataFlow/IfdsIde/Solver/IDESolver.h>
#include <phasar/DataFlow/Mono/Solver/IntraMonoSolver.h>
#include <phasar/PhasarLLVM/TypeHierarchy/DIBasedTypeHierarchy.h>
#include "phasar/Pointer/AliasInfo.h"
#include "phasar/PhasarLLVM/Pointer/LLVMAliasSet.h"

#include "analyses/feasibility/Feasibility.h"

FeasibilityWrapper::FeasibilityWrapper(std::shared_ptr<psr::HelperAnalyses> helperAnalyses,
                                       llvm::FunctionAnalysisManager *analysisManager) {
    if (!helperAnalyses) {
        return;  // Missing helper analyses
    }
    this->FAM = analysisManager;  // Store analysis manager

    llvm::Module *module = helperAnalyses->getProjectIRDB().getModule();  // Project module
    if (!module) {
        llvm::errs() << "[LB] module not found\n";
        return;  // Abort if module is missing
    }

    auto &IRDB = helperAnalyses->getProjectIRDB();
    auto &CFG  = helperAnalyses->getCFG();
    const Feasibility::FeasibilityAnalysisDomain::th_t *TH = &helperAnalyses->getTypeHierarchy();
    auto &PTImpl = helperAnalyses->getAliasInfo();
    psr::AliasInfoRef<const llvm::Value *, const llvm::Instruction *> PT(&PTImpl);

    Feasibility::FeasibilityAnalysis analysisProblem(&IRDB, TH, &CFG, PT);

    psr::IntraMonoSolver<Feasibility::FeasibilityAnalysisDomain> solver(analysisProblem);
    solver.solve();
}
