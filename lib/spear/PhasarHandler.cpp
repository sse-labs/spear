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
#include <deque>
#include <unordered_map>
#include <vector>

#include "analyses/loopbound/LoopBound.h"
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
// We should check if this is really valid here...
      Entrypoints({"__ALL__"}) {}

PreservedAnalyses PhasarHandlerPass::run(Module &M, ModuleAnalysisManager &AM) {
  mod = &M;
  HA = std::make_shared<psr::HelperAnalyses>(&M, Entrypoints);
  LoopBoundResult.reset();
  FeasibilityResult.reset();

  llvm::errs() << M << "\n";

  auto &FAM = AM.getResult<llvm::FunctionAnalysisManagerModuleProxy>(*mod).getManager();

  runAnalysis(M, &FAM);

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

void PhasarHandlerPass::runAnalysis(llvm::Module &M, llvm::FunctionAnalysisManager *FAM) {
  // loopboundwrapper = make_unique<LoopBound::LoopBoundWrapper>(HA, FAM);
  feasibilitywrapper = make_unique<Feasibility::FeasibilityWrapper>(HA, FAM);

  // Store the problem instance for later querying
  feasibilityProblem = feasibilitywrapper->problem;

  // LoopBoundResult = loopboundwrapper->getResults();
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

bool PhasarHandlerPass::constains(std::vector<llvm::BasicBlock *> visited, llvm::BasicBlock *BB) const {
  for (auto *V : visited) {
    if (V == BB) {
      return true;
    }
  }
  return false;
}

Feasibility::FunctionFeasibilityMap PhasarHandlerPass::queryFeasibilty() const {
  Feasibility::FunctionFeasibilityMap FeasibilityInfo;

  for (auto &Func : mod->functions()) {
    if (!Func.isDeclaration()) {
      auto FuncFeasMap = queryFeasibilityOfFunction(&Func);
      if (!FuncFeasMap.empty()) {
        FeasibilityInfo[Func.getName().str()] = std::move(FuncFeasMap);
      }
    }
  }

  return FeasibilityInfo;
}

Feasibility::BlockFeasibilityMap PhasarHandlerPass::queryFeasibilityOfFunction(llvm::Function *Func) const {
  Feasibility::BlockFeasibilityMap BlockFeasibilityMap;

  if (!FeasibilityResult || !Func) {
    return BlockFeasibilityMap;
  }

  const llvm::Value *Zero = feasibilityProblem ? feasibilityProblem->getZeroValue() : nullptr;

  // Create the worklist and visited set for a simple CFG traversal to query feasibility at block entries.
  auto firstBlock = &Func->getEntryBlock();
  std::deque<llvm::BasicBlock*> worklist{firstBlock};
  llvm::DenseMap<const llvm::BasicBlock*, Feasibility::BlockFeasInfo> visited;
  std::unordered_map<SetSatnessKey, bool, SetSatnessHash> SatCache;
  SatCache.reserve(128);

  // At all blocks to the visited set with default entries to ensure we don't revisit them.
  // We will fill in the actual feasibility info in the next loop.
  for (auto &BB : *Func) {
    const std::string BBName = blockName(BB);
    // Initialize each entry
    visited[&BB] = Feasibility::BlockFeasInfo{};
  }

  // Iterate over the worklist until its empty
  while (!worklist.empty()) {
    // Get the first entry
    llvm::BasicBlock *BB = worklist.front();
    worklist.pop_front();

    // Check if we have already visited this block. If so, skip it.
    const std::string BBName = blockName(*BB);

    // If we have already visited this block, skip it to avoid infinite loops in cyclic CFGs.
    if (visited[BB].visited) {
      continue;
    }

    // Mark this block as visited
    visited[BB].visited = true;

    // Query feasibility at the last instruction of the block (terminator),
    // which is the most stable point to query for block entry feasibility.
    if (const llvm::Instruction *Term = BB->getTerminator()) {
      // Query the analysis result for the terminator instruction and check if it contains an entry for the zero value.
      auto res = FeasibilityResult->resultsAt(Term);
      auto it = res.find(Zero);

      if (it != res.end()) {
        // If it does, we check the kind of the lattice element. If it's not bottom, the block is feasible.
        const auto &entry = it->second;

        auto *Mgr = entry.getManager();
        uint32_t FId = entry.getFormulaId();

        std::vector<z3::expr> set = Mgr->getPureSet(FId);
        SetSatnessKey Sig = makeSetSattnessCacheEntry(Mgr, set);

        auto ItSat = SatCache.find(Sig);
        bool isSat = false;

        if (ItSat != SatCache.end()) {
          isSat = ItSat->second;
        } else {
          isSat = Feasibility::Util::setSat(set, &Mgr->getContext());
          SatCache.emplace(std::move(Sig), isSat);
        }

        BlockFeasibilityMap[BBName].Feasible = isSat;
        BlockFeasibilityMap[BBName].HasZeroAtEntry = true;
        BlockFeasibilityMap[BBName].visited = true;

        if (isSat) {
          auto sucss = llvm::successors(BB);
          worklist.insert(worklist.end(), sucss.begin(), sucss.end());
        }
      }
    }
  }

  return BlockFeasibilityMap;
}


std::string PhasarHandlerPass::blockName(const llvm::BasicBlock &BB) {
  return BB.hasName()
             ? BB.getName().str()
             : "<unnamed_bb_" + std::to_string(reinterpret_cast<uintptr_t>(&BB)) +
                   ">";
}
