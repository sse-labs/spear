//
// PhasarHandler - LLVM new-pass-manager compatible PhASAR pass
//

#ifndef SPEAR_PHASARHANDLER_H
#define SPEAR_PHASARHANDLER_H

#include <map>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#include <phasar.h> // whatever umbrella header you already use

#include <llvm/IR/PassManager.h>

namespace llvm {
class Module;
class Function;
class Value;
class Instruction;
} // namespace llvm

/// A ModulePass that runs PhASAR's IDELinearConstantAnalysis and provides
/// a helper to query the resulting lattice values for each basic block.
///
/// This is written for the LLVM "new" pass manager (PassBuilder).
class PhasarHandlerPass : public llvm::PassInfoMixin<PhasarHandlerPass> {
public:
  using DomainVal = psr::IDELinearConstantAnalysisDomain::l_t;

  /// Result:  BB_name -> { var_name -> (Value*, domain_val) }
  using BoundVarMap =
      std::map<std::string,
               std::map<std::string,
                        std::pair<const llvm::Value *, DomainVal>>>;

  PhasarHandlerPass();

  /// New pass manager entry point
  llvm::PreservedAnalyses run(llvm::Module &M,
                              llvm::ModuleAnalysisManager &AM);

  /// Debug helper: dump full PhASAR results
  void dumpState() const;

  void runOnModule(llvm::Module &M);

  /// Query all "bound variables" in a function, grouped per basic block.
  /// Requires that `run()` (and hence `runAnalysis()`) has been executed.
  BoundVarMap queryBoundVars(llvm::Function *Func) const;

private:
  /// Backing module – only valid during/after `run()`.
  llvm::Module *mod;

  /// PhASAR helper analyses
  std::unique_ptr<psr::HelperAnalyses> HA;

  /// Solver results: PhASAR’s IDE solver result wrapper.
  std::unique_ptr<psr::OwningSolverResults<
      const llvm::Instruction *, const llvm::Value *, psr::LatticeDomain<int64_t>>>
      AnalysisResult;

  /// Function entrypoints
  std::vector<std::string> Entrypoints;

  /// Internal: construct analysis problem and run PhASAR solver.
  void runAnalysis();
};

#endif // SPEAR_PHASARHANDLER_H
