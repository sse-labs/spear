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
#include <phasar/DataFlow/IfdsIde/IDETabulationProblem.h>
#include <phasar/PhasarLLVM/DB/LLVMProjectIRDB.h>
#include <phasar/PhasarLLVM/ControlFlow/LLVMBasedICFG.h>
#include <llvm/Support/raw_ostream.h>


#include "analyses/feasibility/FeasibilityAnalysis.h"
#include "analyses/feasibility/util.h"

Feasibility::FeasibilityWrapper::FeasibilityWrapper(std::shared_ptr<psr::HelperAnalyses> helperAnalyses,
                                                    llvm::FunctionAnalysisManager *analysisManager) {
    if (!helperAnalyses) {
        return; // Missing helper analyses
    }
    this->FAM = analysisManager; // Store analysis manager

    llvm::Module *module = helperAnalyses->getProjectIRDB().getModule(); // Project module
    if (!module) {
        llvm::errs() << "[LB] module not found\n";
        return; // Abort if module is missing
    }

    auto &IRDB = helperAnalyses->getProjectIRDB();
    auto &interproceduralCFG = helperAnalyses->getICFG(); // Interprocedural CFG
    auto &PTImpl = helperAnalyses->getAliasInfo();
    psr::AliasInfoRef<const llvm::Value *, const llvm::Instruction *> PT(&PTImpl);

    this->problem = std::make_shared<Feasibility::FeasibilityAnalysis>(Feasibility::FeasibilityAnalysis(analysisManager, &IRDB, &interproceduralCFG));

    if (F_DEBUG_ENABLED) {
        llvm::errs() << F_TAG << " Starting IDESolver.solve()\n";
    }

    auto analysisResult = psr::solveIDEProblem(*this->problem.get(), interproceduralCFG);
    this->cachedResults = std::make_unique<ResultsTy>(std::move(analysisResult));

    if (F_DEBUG_ENABLED) {
        llvm::errs() << F_TAG << " Finished IDESolver.solve()\n";
    }
}

std::unique_ptr<Feasibility::ResultsTy> Feasibility::FeasibilityWrapper::getResults() const {
    return std::make_unique<ResultsTy>(*this->cachedResults);
}
