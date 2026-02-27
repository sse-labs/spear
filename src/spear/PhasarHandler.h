/*
 * Copyright (c) 2026 Maximilian Krebs
 * All rights reserved.
*/

#ifndef SRC_SPEAR_PHASARHANDLER_H_
#define SRC_SPEAR_PHASARHANDLER_H_

#include <phasar.h>
#include <llvm/IR/PassManager.h>
#include <analyses/loopbound/loopBoundWrapper.h>

#include <map>
#include <memory>
#include <string>
#include <utility>
#include <vector>
#include <unordered_map>

#include "analyses/feasibility/FeasibilityElement.h"
#include "analyses/feasibility/FeasibilitySetSatness.h"
#include "analyses/feasibility/FeasibilityWrapper.h"
#include "analyses/loopbound/LoopBound.h"


namespace llvm {
class Module;
class Function;
class Value;
class Instruction;
}  // namespace llvm

/**
 * Struct to store the feasibility information for a basic block,
 * including whether it is feasible, whether it has a zero value at entry, and whether it has
 * been visited during analysis.
 */
struct BlockFeasInfo {
    bool Feasible = false;
    Feasibility::FeasibilityElement ZeroAtEntry;
    bool HasZeroAtEntry = false;
    bool visited = false;
};

// A ModulePass that runs PhASAR's IDELinearConstantAnalysis and provides
// a helper to query the resulting lattice values for each basic block.
//
// This is written for the LLVM "new" pass manager (PassBuilder).
class PhasarHandlerPass : public llvm::PassInfoMixin<PhasarHandlerPass> {
 public:
    using LoopBoundDomainVal = LoopBound::DeltaInterval;
    using FeasibilityDomainVal = Feasibility::FeasibilityElement;

    using BoundVarMap = std::map<std::string,
        std::map<std::string, std::pair<const llvm::Value *, LoopBoundDomainVal>>>;

    using FeasibilityMap = std::unordered_map<std::string, BlockFeasInfo>;

    PhasarHandlerPass();

    // New pass manager entry point
    llvm::PreservedAnalyses run(llvm::Module &M, llvm::ModuleAnalysisManager &AM);

    // Debug helper: dump full PhASAR results
    void dumpState() const;

    void runOnModule(llvm::Module &M);

    // Query all "bound variables" in a function, grouped per basic block.
    // Requires that `run()` (and hence `runAnalysis()`) has been executed.
    BoundVarMap queryBoundVars(llvm::Function *Func) const;
    FeasibilityMap queryFeasibility(llvm::Function *Func) const;

    std::unique_ptr<LoopBound::LoopBoundWrapper> loopboundwrapper = nullptr;
    std::unique_ptr<Feasibility::FeasibilityWrapper> feasibilitywrapper = nullptr;
    std::shared_ptr<Feasibility::FeasibilityAnalysis> feasibilityProblem;

    bool constains(std::vector<llvm::BasicBlock *> visited, llvm::BasicBlock *BB) const;

 private:
    // Backing module – only valid during/after `run()`.
    llvm::Module *mod;

    // PhASAR helper analyses
    std::shared_ptr<psr::HelperAnalyses> HA;

    // Solver results: PhASAR’s IDE solver result wrapper.
    std::unique_ptr<psr::OwningSolverResults<const llvm::Instruction *, const llvm::Value *, LoopBound::DeltaInterval>>
    LoopBoundResult;

    std::unique_ptr<psr::OwningSolverResults<const llvm::Instruction *, const llvm::Value *,
    Feasibility::FeasibilityElement>>
    FeasibilityResult;

    // Function entrypoints
    std::vector<std::string> Entrypoints;

    /**
     * Execute the analysis on the given module, using the provided FunctionAnalysisManager to
     * retrieve necessary analyses.
     * @param M Module to run the analysis on
     * @param FAM FunctionAnalysisManager to retrieve necessary analyses for the PhASAR analysis problem
     */
    void runAnalysis(llvm::Module &M, llvm::FunctionAnalysisManager *FAM);

    /**
     * Get the name of the given basic block.
     * @param BB Basic block to get the name of
     * @return string name of the given basic block. If the block has no name, a unique name will be
     * generated based on the block's address.
     */
    static std::string blockName(const llvm::BasicBlock &BB);

    static inline SetSatnessKey makeSetSattnessCacheEntry(
                            const Feasibility::FeasibilityAnalysisManager *Mgr,
                            const std::vector<z3::expr> &Set) {
        // Create a cache key for the given set of formulas by hashing their AST ids in sorted order.
        SetSatnessKey K;
        K.Mgr = Mgr;
        K.AstIds.reserve(Set.size());

        // Inser the z3 expressions from the given set to the cache key as their AST ids, which uniquely identify
        // the expressions. We need to sort the AST ids to ensure that the order of the formulas in the s
        // et does not affect the hash value.
        for (const z3::expr &E : Set) {
            K.AstIds.push_back(Z3_get_ast_id(E.ctx(), E));
        }

        // Sort the AST ids to ensure that the order of the formulas in the set does not affect the hash value.
        llvm::sort(K.AstIds);

        // Establish uniqueness on the aST ids to ensure that the same set of formulas always results in the
        // same cache key, even if it contains duplicate formulas.
        K.AstIds.erase(std::unique(K.AstIds.begin(), K.AstIds.end()), K.AstIds.end());
        return K;
    }
};

#endif  // SRC_SPEAR_PHASARHANDLER_H_
