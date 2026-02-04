/*
 * Copyright (c) 2026 Maximilian Krebs
 * All rights reserved.
*/

#ifndef SRC_SPEAR_PHASARHANDLER_H_
#define SRC_SPEAR_PHASARHANDLER_H_

#include <phasar.h>
#include <llvm/IR/PassManager.h>

#include <map>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#include "analyses/loopbound/LoopBound.h"
#include "analyses/loopbound/LoopBoundEdgeFunction.h"

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
  using DomainVal = LoopBound::DeltaInterval;

  // Result:  BB_name -> { var_name -> (Value*, domain_val) }
  using BoundVarMap =
      std::map<std::string,
               std::map<std::string,
                        std::pair<const llvm::Value *, DomainVal>>>;

  PhasarHandlerPass();

  // New pass manager entry point
  llvm::PreservedAnalyses run(llvm::Module &M,
                              llvm::ModuleAnalysisManager &AM);

  // Debug helper: dump full PhASAR results
  void dumpState() const;

  void runOnModule(llvm::Module &M);

  // Query all "bound variables" in a function, grouped per basic block.
  // Requires that `run()` (and hence `runAnalysis()`) has been executed.
  BoundVarMap queryBoundVars(llvm::Function *Func) const;

 private:
  // Backing module – only valid during/after `run()`.
  llvm::Module *mod;

  // PhASAR helper analyses
  std::unique_ptr<psr::HelperAnalyses> HA;

  // Solver results: PhASAR’s IDE solver result wrapper.
  std::unique_ptr<psr::OwningSolverResults<
      const llvm::Instruction *, const llvm::Value *, LoopBound::DeltaInterval>>
      AnalysisResult;

  // Function entrypoints
  std::vector<std::string> Entrypoints;

  // Internal: construct analysis problem and run PhASAR solver.
  void runAnalysis(llvm::FunctionAnalysisManager *FAM);
};

#endif  // SRC_SPEAR_PHASARHANDLER_H_
