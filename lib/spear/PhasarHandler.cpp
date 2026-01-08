#include "PhasarHandler.h"

#include <llvm/IR/BasicBlock.h>
#include <llvm/IR/Function.h>
#include <llvm/IR/Instruction.h>
#include <llvm/IR/Module.h>

#include <llvm/Passes/PassBuilder.h>
#include <llvm/Passes/PassPlugin.h>

using namespace llvm;

PhasarHandlerPass::PhasarHandlerPass()
    : mod(nullptr),
      HA(nullptr),
      AnalysisResult(nullptr),
      Entrypoints({std::string("main")}) {}

PreservedAnalyses PhasarHandlerPass::run(Module &M, ModuleAnalysisManager &AM) {
  // Store module and build PhASAR helper.
  mod = &M;
  HA = std::make_unique<psr::HelperAnalyses>(&M, Entrypoints);
  AnalysisResult.reset();

  // Actually run the IDELinearConstantAnalysis
  runAnalysis();

  // If this pass only computes information and doesn't modify the IR, we can
  // conservatively say everything is preserved.
  return PreservedAnalyses::all();
}

void PhasarHandlerPass::runOnModule(llvm::Module &M) {
  // Create a dummy module analysis manager so the regular run() entry point works
  llvm::ModuleAnalysisManager DummyAM;
  run(M, DummyAM);
}


void PhasarHandlerPass::runAnalysis() {
  if (!HA)
    return;

  // Ensure we actually have a 'main' entry point
  if (!HA->getProjectIRDB().getFunctionDefinition("main"))
    return;

  // Build the analysis problem and solve it
  auto Problem = psr::createAnalysisProblem<psr::IDELinearConstantAnalysis>(
      *HA, Entrypoints);

  // Alternative way of solving an IFDS/IDEProblem:
  auto Result = psr::solveIDEProblem(Problem, HA->getICFG());

  //Result.dumpResults(HA->getICFG(), llvm::outs());

  AnalysisResult = std::make_unique<psr::OwningSolverResults<
      const llvm::Instruction *, const llvm::Value *, psr::LatticeDomain<int64_t>>>(
      Result);
}

void PhasarHandlerPass::dumpState() const {
  if (AnalysisResult && HA) {
    AnalysisResult->dumpResults(HA->getICFG());
  }
}

PhasarHandlerPass::BoundVarMap
PhasarHandlerPass::queryBoundVars(llvm::Function *Func) const {
  BoundVarMap ResultMap;

  if (!AnalysisResult || !Func)
    return ResultMap;

  using DomainVal = psr::IDELinearConstantAnalysisDomain::l_t;

  // Result:  BB_name -> { var_name -> (Value*, domain_val) }
  for (const llvm::BasicBlock &BB : *Func) {
    std::string BBName = BB.hasName()
                             ? BB.getName().str()
                             : "<unnamed_bb_" +
                                   std::to_string(
                                       reinterpret_cast<uintptr_t>(&BB)) +
                                   ">";

    // Ensure block entry exists
    auto &BBEntry = ResultMap[BBName];

    for (const llvm::Instruction &Inst : BB) {
      if (!AnalysisResult->containsNode(&Inst))
        continue;

      psr::LLVMAnalysisDomainDefault::d_t Bottom = nullptr;
      auto Res = AnalysisResult->resultsAtInLLVMSSA(&Inst, Bottom);

      for (const auto &ResElement : Res) {
        const llvm::Value *Val = ResElement.first;
        const DomainVal &DomVal = ResElement.second;

        std::string Key = Val->hasName()
                              ? Val->getName().str()
                              : "<unnamed_" +
                                    std::to_string(
                                        reinterpret_cast<uintptr_t>(Val)) +
                                    ">";

        BBEntry[Key] = std::make_pair(Val, DomVal);
      }
    }
  }

  return ResultMap;
}