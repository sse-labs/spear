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

#include "../../src/spear/analyses/loopbound/LoopBound.h"
#include "../../src/spear/analyses/loopbound/util.h"
#include "analyses/loopbound/loopBoundWrapper.h"

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

  auto &FAM =
      AM.getResult<llvm::FunctionAnalysisManagerModuleProxy>(*mod).getManager();

  runAnalysis(&FAM);

  return PreservedAnalyses::all();
}

void PhasarHandlerPass::runOnModule(llvm::Module &M) {
  llvm::PassBuilder PB;

  llvm::LoopAnalysisManager LAM;
  llvm::FunctionAnalysisManager FAM;
  llvm::CGSCCAnalysisManager CGAM;
  llvm::ModuleAnalysisManager MAM;

  PB.registerModuleAnalyses(MAM);
  PB.registerCGSCCAnalyses(CGAM);
  PB.registerFunctionAnalyses(FAM);
  PB.registerLoopAnalyses(LAM);

  PB.crossRegisterProxies(LAM, FAM, CGAM, MAM);

  run(M, MAM);
}

void PhasarHandlerPass::runAnalysis(llvm::FunctionAnalysisManager *FAM) {
  LoopBoundWrapper lbw((std::move(HA)), FAM);
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

  using DomainVal = LoopBound::DeltaInterval;

  for (const llvm::BasicBlock &BB : *Func) {
    std::string BBName =
        BB.hasName()
            ? BB.getName().str()
            : "<unnamed_bb_" +
                  std::to_string(reinterpret_cast<uintptr_t>(&BB)) + ">";

    auto &BBEntry = ResultMap[BBName];

    for (const llvm::Instruction &Inst : BB) {
      if (!AnalysisResult->containsNode(&Inst))
        continue;

      const llvm::Value *Bottom = nullptr;
      auto Res = AnalysisResult->resultsAtInLLVMSSA(&Inst, Bottom);

      for (const auto &ResElement : Res) {
        const llvm::Value *Val = ResElement.first;
        const DomainVal &DomVal = ResElement.second;

        std::string Key =
            Val->hasName()
                ? Val->getName().str()
                : "<unnamed_" + std::to_string(reinterpret_cast<uintptr_t>(Val)) +
                      ">";

        BBEntry[Key] = std::make_pair(Val, DomVal);
      }
    }
  }

  return ResultMap;
}
