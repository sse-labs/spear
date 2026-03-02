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
#include <vector>
#include <unordered_map>

#include "analyses/feasibility/FeasibilityElement.h"
#include "analyses/feasibility/FeasibilityWrapper.h"
#include "analyses/loopbound/LoopBound.h"


namespace llvm {
class Module;
class Function;
class Value;
class Instruction;
}  // namespace llvm

/**
 * Analysis configuration struct. Allows us to configure the analysis execution
 */
struct AnalysisConfig {
    bool RUNLOOPBOUNDANALYSIS = true;
    bool RUNFEASIBILITYANALYSIS = true;
    bool SHOWDEBUGOUTPUT = false;
};


/**
 * The PhasarHandlerPass used to run our Phasar-based analyses. This pass can be added to the llvm pass manager and
 * will run the configured analyses on the module under analysis.
 */
class PhasarHandlerPass : public llvm::PassInfoMixin<PhasarHandlerPass> {
 public:
    using LoopBoundDomainVal = LoopBound::DeltaInterval;
    using FeasibilityDomainVal = Feasibility::FeasibilityElement;

    /**
     * Main constructor. Allows to configure whether the loop bound and feasibility analysis should be executed and
     * whether debug output should be printed.
     * @param runLoopBoundAnalysis Whether the loop bound analysis should be executed
     * @param runFeasibilityAnalysis Whether the feasibility analysis should be executed
     * @param showDebugOutput Whether debug output should be printed during the analysis execution
     */
    PhasarHandlerPass(
        bool runLoopBoundAnalysis = false,
        bool runFeasibilityAnalysis = false,
        bool showDebugOutput = false);

    /**
     * Run the analysis on the given module, using the provided ModuleAnalysisManager to retrieve necessary analyses for
     * the PhASAR analysis problem. The results of the analysis will be stored internally and
     * @param M Module to run the analysis on
     * @param AM ModuleAnalysisManager to retrieve necessary analyses for the PhASAR analysis problem
     * @return A PreservedAnalyses object indicating which analyses are preserved after running this pass.
     * In our case, we return PreservedAnalyses::all() to indicate that all analyses are preserved, as our pass does
     * not modify the module in a way that would invalidate any analyses.
     */
    llvm::PreservedAnalyses run(llvm::Module &M, llvm::ModuleAnalysisManager &AM);

    /**
     * Run the analysis on the given module.
     * @param M Module to run the analysis on
     */
    void runOnModule(llvm::Module &M);

    /**
     * Query the loop bound information for all functions in the module
     * @return A LoopFunctionMap containing the loop bound information for all functions in the module,
     * where each function name maps to a LoopToBoundMap that contains the loop names and their corresponding bounds.
     */
    LoopBound::LoopFunctionMap queryLoopBounds() const;

    /**
     * Query the loop bound information for each loop in the given function, returning a mapping from loop names to
     * their corresponding bounds.
     * @param Func Function to query the loop bound information for
     * @return LoopToBoundMap containing the loop bound information for each loop in the given function,
     * where each loop name maps to its corresponding bound.
     */
    LoopBound::LoopToBoundMap queryBoundsOfFunction(llvm::Function *Func) const;

    /**
     * Query the feasibility information for all functions in the module
     * @return Mapping from function names to their basic block feasibility information
     */
    Feasibility::FunctionFeasibilityMap queryFeasibilty() const;

    /**
     * Query the feasibility information for each basic block of the given function,
     * including whether the block is feasible and whether it has a zero value at entry.
     * @param Func Function to query the feasibility information for
     * @return A map from basic block names to their feasibility information
     */
    Feasibility::BlockFeasibilityMap queryFeasibilityOfFunction(llvm::Function *Func) const;

    /**
     * Pointer to the loop bound analysis wrapper, which provides helper functions to query the analysis results and
     * access the underlying PhASAR analysis problem instance.
     */
    std::unique_ptr<LoopBound::LoopBoundWrapper> loopboundwrapper = nullptr;

    /**
     * Pointer to the loop bound analysis problem instance, which contains the actual implementation of the analysis and
     * the internal representation of the loops and their parameters.
     */
    std::shared_ptr<LoopBound::LoopBoundIDEAnalysis> loopboundProblem;

    /**
     * Pointer to the feasibility analysis wrapper, which provides helper functions to query the analysis results and
     * access the underlying PhASAR analysis problem instance.
     */
    std::unique_ptr<Feasibility::FeasibilityWrapper> feasibilitywrapper = nullptr;

    /**
     * Pointer to the feasibility analysis problem instance, which contains the actual implementation of the analysis
     * and the internal representation of the basic blocks and their feasibility information.
     */
    std::shared_ptr<Feasibility::FeasibilityAnalysis> feasibilityProblem;

 private:
    /**
     * Function entry points to start the analysis from.
     */
    std::vector<std::string> Entrypoints;

    /**
     * Configuration options for the analysis, set by the constructor and used to control which analyses are executed
     * and whether debug output is printed during the analysis execution.
     */
    AnalysisConfig config;

    /**
     * Module under analysis, stored as a pointer for easier access throughout the class.
     */
    llvm::Module *mod;

    /**
     * Helper analyses provided by PhASAR, which allow us to easily retrieve necessary analyses for our analysis
     * problems
     */
    std::shared_ptr<psr::HelperAnalyses> HA;

    /**
     * Internal results of the loop bound analysis
     */
    std::unique_ptr<psr::OwningSolverResults<const llvm::Instruction *, const llvm::Value *, LoopBound::DeltaInterval>>
    LoopBoundResult;

    /**
    * Internal results of the feasibility analysis
    */
    std::unique_ptr<psr::OwningSolverResults<const llvm::Instruction *, const llvm::Value *,
    Feasibility::FeasibilityElement>>
    FeasibilityResult;

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
};

#endif  // SRC_SPEAR_PHASARHANDLER_H_
