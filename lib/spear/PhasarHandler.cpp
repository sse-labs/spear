/*
 * Copyright (c) 2026 Maximilian Krebs
 * All rights reserved.
*/

#include "PhasarHandler.h"

#include <llvm/IR/BasicBlock.h>
#include <llvm/IR/Function.h>
#include <llvm/IR/Instruction.h>
#include <llvm/IR/Module.h>

#include <llvm/Passes/PassBuilder.h>
#include <llvm/Passes/PassPlugin.h>

#include <utility>
#include <string>
#include <memory>

#include "analyses/loopbound.h"

using llvm::Module;
using llvm::PreservedAnalyses;
using llvm::ModuleAnalysisManager;

PhasarHandlerPass::PhasarHandlerPass()
    : mod(nullptr),
      HA(nullptr),
      AnalysisResult(nullptr),
      Entrypoints({std::string("main")}) {}

PreservedAnalyses PhasarHandlerPass::run(Module &M, ModuleAnalysisManager &AM) {
  mod = &M;
  HA = std::make_unique<psr::HelperAnalyses>(&M, Entrypoints);
  AnalysisResult.reset();

  runAnalysis();

  return PreservedAnalyses::all();
}

void PhasarHandlerPass::runOnModule(llvm::Module &M) {
  ModuleAnalysisManager DummyAM;
  run(M, DummyAM);
}

void PhasarHandlerPass::runAnalysis() {
  if (!HA) return;
  if (!HA->getProjectIRDB().getFunctionDefinition("main")) return;

  loopbound::LoopBoundIDEAnalysis Problem(
    HA->getProjectIRDB(),
    HA->getCFG(),          // <-- ADD THIS
    Entrypoints);

  using ResultsTy = psr::OwningSolverResults<
      const llvm::Instruction *, const llvm::Value *, loopbound::DeltaInterval>;

  auto &ICFG = HA->getICFG();

  const llvm::Function *F = HA->getProjectIRDB().getFunctionDefinition("main");
  auto *SeedI = &F->getEntryBlock().front();

  llvm::outs() << "[LB] SeedI: " << *SeedI << "\n";
  llvm::outs() << "[LB] Succs of SeedI:\n";
  for (auto *Succ : ICFG.getSuccsOf(SeedI)) {
    llvm::outs() << "  - " << *Succ << "\n";
  }
  llvm::outs() << "[LB] Preds of SeedI:\n";
  for (auto *Pred : ICFG.getPredsOf(SeedI)) {
    llvm::outs() << "  - " << *Pred << "\n";
  }

  auto Result = psr::solveIDEProblem(Problem, HA->getICFG());
  AnalysisResult = std::make_unique<ResultsTy>(std::move(Result));
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

  using DomainVal = loopbound::DeltaInterval;

  for (const llvm::BasicBlock &BB : *Func) {
    std::string BBName = BB.hasName()
                             ? BB.getName().str()
                             : "<unnamed_bb_" +
                                   std::to_string(
                                       reinterpret_cast<uintptr_t>(&BB)) +
                                   ">";

    auto &BBEntry = ResultMap[BBName];

    for (const llvm::Instruction &Inst : BB) {
      if (!AnalysisResult->containsNode(&Inst))
        continue;

      const llvm::Value *Bottom = nullptr;
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
