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

#include "analyses/feasibility/FeasibilityElement.h"
#include "analyses/feasibility/FeasibilityWrapper.h"
#include "analyses/loopbound/LoopBound.h"


namespace llvm {
class Module;
class Function;
class Value;
class Instruction;
}  // namespace llvm

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

    struct BlockFeasInfo {
        bool Feasible = false;
        Feasibility::FeasibilityElement ZeroAtEntry; // optional
        bool HasZeroAtEntry = false;
    };
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

 private:

    // Backing module – only valid during/after `run()`.
    llvm::Module *mod;

    // PhASAR helper analyses
    std::shared_ptr<psr::HelperAnalyses> HA;

    // Solver results: PhASAR’s IDE solver result wrapper.
    std::unique_ptr<psr::OwningSolverResults<const llvm::Instruction *, const llvm::Value *, LoopBound::DeltaInterval>>
    LoopBoundResult;

    std::unique_ptr<psr::OwningSolverResults<const llvm::Instruction *, const llvm::Value *, Feasibility::FeasibilityElement>>
    FeasibilityResult;

    // Function entrypoints
    std::vector<std::string> Entrypoints;

    // Internal: construct analysis problem and run PhASAR solver.
    void runAnalysis(llvm::FunctionAnalysisManager *FAM);

    static std::string blockName(const llvm::BasicBlock &BB);
    static const llvm::Instruction *pickQueryInst(const llvm::BasicBlock &BB);

    bool isNodeFeasibleForZero(const llvm::Instruction *I,
                               const llvm::Value *Zero) const;

    std::optional<Feasibility::FeasibilityAnalysis::l_t> getLatticeAtNodeForZero(
        const llvm::Instruction *I, const llvm::Value *Zero) const;

    const llvm::Instruction *pickSuccNodeFromICFG(const llvm::Instruction *PredTerm,
                                                  const llvm::BasicBlock *SuccBB) const;

    bool isEdgeFeasible(const llvm::BasicBlock *PredBB,
                        const llvm::BasicBlock *SuccBB,
                        const llvm::Value *Zero) const;
};

#endif  // SRC_SPEAR_PHASARHANDLER_H_
