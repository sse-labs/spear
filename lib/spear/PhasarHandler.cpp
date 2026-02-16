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
#include <catch2/internal/catch_unique_ptr.hpp>

#include "../../src/spear/analyses/loopbound/LoopBound.h"
#include "../../src/spear/analyses/loopbound/util.h"
#include "analyses/feasibility/util.h"
#include "analyses/loopbound/loopBoundWrapper.h"

using llvm::Module;
using llvm::PreservedAnalyses;
using llvm::ModuleAnalysisManager;

PhasarHandlerPass::PhasarHandlerPass()
    : mod(nullptr),
      HA(nullptr),
      LoopBoundResult(nullptr),
      FeasibilityResult(nullptr),
      Entrypoints({std::string("main")}) {}

PreservedAnalyses PhasarHandlerPass::run(Module &M, ModuleAnalysisManager &AM) {
  mod = &M;
  HA = std::make_shared<psr::HelperAnalyses>(&M, Entrypoints);
  LoopBoundResult.reset();
  FeasibilityResult.reset();

  auto &FAM = AM.getResult<llvm::FunctionAnalysisManagerModuleProxy>(*mod).getManager();

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
  loopboundwrapper = make_unique<LoopBound::LoopBoundWrapper>(HA, FAM);
  feasibilitywrapper = make_unique<Feasibility::FeasibilityWrapper>(HA, FAM);

  feasibilityProblem = feasibilitywrapper->problem; // Store the problem instance for later querying

  LoopBoundResult = loopboundwrapper->getResults();
  FeasibilityResult = feasibilitywrapper->getResults();
}

void PhasarHandlerPass::dumpState() const {
  if (LoopBoundResult && HA) {
    LoopBoundResult->dumpResults(HA->getICFG());
  }
}

PhasarHandlerPass::BoundVarMap PhasarHandlerPass::queryBoundVars(llvm::Function *Func) const {
  BoundVarMap ResultMap;

  if (!LoopBoundResult || !Func)
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
      if (!LoopBoundResult->containsNode(&Inst))
        continue;

      const llvm::Value *Bottom = nullptr;
      auto Res = LoopBoundResult->resultsAtInLLVMSSA(&Inst, Bottom);

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

PhasarHandlerPass::FeasibilityMap
PhasarHandlerPass::queryFeasibility(llvm::Function *Func) const {
  FeasibilityMap ResultMap;

  if (!FeasibilityResult || !Func) {
    return ResultMap;
  }

  const llvm::Value *Zero =
      feasibilityProblem ? feasibilityProblem->getZeroValue() : nullptr;

  for (const llvm::BasicBlock &BB : *Func) {
    const std::string BBName =
        BB.hasName()
            ? BB.getName().str()
            : "<unnamed_bb_" +
                  std::to_string(reinterpret_cast<uintptr_t>(&BB)) + ">";

    BlockFeasInfo &Info = ResultMap[BBName];

    Info.Feasible = false;
    Info.HasZeroAtEntry = false;

    if (BB.empty()) {
      continue;
    }

    const llvm::Instruction *EntryI = Feasibility::Util::firstRealInst(&BB);
    if (!EntryI) continue;

    // If block is just a trampoline (terminator-only), don't decide feasibility here
    if (EntryI->isTerminator()) {
      Info.Feasible = false; // or compute via predecessors (recommended)
      continue;
    }

    if (!FeasibilityResult->containsNode(EntryI)) {
      continue;
    }

    // fact -> l_t
    const auto ResMap = FeasibilityResult->resultsAt(EntryI);

    // Diagnostics: do we have zero at entry?
    if (Zero) {
      const auto ItZ = ResMap.find(Zero);
      if (ItZ != ResMap.end()) {
        Info.ZeroAtEntry = ItZ->second;
        Info.HasZeroAtEntry = true;
      }
    }

    bool Feasible = false;

    if (Zero) {
      auto It = ResMap.find(Zero);
      if (It != ResMap.end()) {
        const auto &L = It->second;
        Feasible = !L.isBottom() && !L.isIdeAbsorbing() && L.isSatisfiable();
      }
    }

    Info.Feasible = Feasible;
  }

  return ResultMap;
}
