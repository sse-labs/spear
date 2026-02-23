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

  llvm::errs() << M << "\n";

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
  //loopboundwrapper = make_unique<LoopBound::LoopBoundWrapper>(HA, FAM);
  feasibilitywrapper = make_unique<Feasibility::FeasibilityWrapper>(HA, FAM);

  feasibilityProblem = feasibilitywrapper->problem; // Store the problem instance for later querying

  //LoopBoundResult = loopboundwrapper->getResults();
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

  const llvm::Value *Zero = feasibilityProblem ? feasibilityProblem->getZeroValue() : nullptr;

  // 1) Create entries + optional diagnostics
  for (const llvm::BasicBlock &BB : *Func) {
    const std::string BBName = blockName(BB);
    BlockFeasInfo &Info = ResultMap[BBName];

    Info.Feasible = false;
    Info.HasZeroAtEntry = false;

    for (auto &I : BB) {
      auto latticeElementExists = FeasibilityResult->containsNode(&I);
      if (latticeElementExists) {
        auto ResMap = FeasibilityResult->resultsAt(&I);
        for (auto entry : ResMap) {
          auto k = entry.second.getKind();
          std::string kindStr = (k == Feasibility::FeasibilityElement::Kind::Top)
                                ? "TOP"
                                : (k == Feasibility::FeasibilityElement::Kind::Bottom)
                                      ? "BOTTOM"
                                      : "NORMAL";

          llvm::outs() << "Feasibility at instruction " << I << ": " << kindStr << ", id: " << entry.second.getFormulaId() << "\n";
        }
      }
    }

    llvm::outs() << "\n";


    const llvm::Instruction *terminator = BB.getTerminator();
    auto latticeElementExists = FeasibilityResult->containsNode(terminator);
    if (latticeElementExists) {
      auto ResMap = FeasibilityResult->resultsAt(terminator);
      auto ItZ = ResMap.find(Zero);
      if (ItZ != ResMap.end()) {
        const auto &L = ItZ->second;
        //llvm::outs() << "Feasibility at terminator of " << BBName << ": " << L.getKind() << "\n";
        if (L.isBottom()) {
          Info.Feasible = true;
        }
      }
    }
  }



  return ResultMap;
}


std::string PhasarHandlerPass::blockName(const llvm::BasicBlock &BB) {
  return BB.hasName()
             ? BB.getName().str()
             : "<unnamed_bb_" + std::to_string(reinterpret_cast<uintptr_t>(&BB)) +
                   ">";
}

// Pick a stable instruction to query inside a block:
// - first non-PHI instruction if present
// - otherwise the terminator
const llvm::Instruction *PhasarHandlerPass::pickQueryInst(
    const llvm::BasicBlock &BB) {
  const llvm::BasicBlock *Cur = &BB;
  llvm::SmallPtrSet<const llvm::BasicBlock *, 8> Visited;

  while (Cur && !Visited.contains(Cur)) {
    Visited.insert(Cur);

    if (!Cur->empty()) {
      // Prefer first non-PHI
      for (const llvm::Instruction &I : *Cur) {
        if (!llvm::isa<llvm::PHINode>(&I)) return &I;
      }
      // otherwise terminator
      const llvm::Instruction *T = Cur->getTerminator();
      // If it's an unconditional trampoline, follow it
      if (auto *Br = llvm::dyn_cast<llvm::BranchInst>(T)) {
        if (!Br->isConditional()) {
          Cur = Br->getSuccessor(0);
          continue;
        }
      }
      return T;
    }

    return nullptr;
  }

  // loop / weird CFG: fall back
  return BB.getTerminator();
}


bool PhasarHandlerPass::isNodeFeasibleForZero(const llvm::Instruction *I,
                                              const llvm::Value *Zero) const {
  if (!FeasibilityResult || !I || !Zero)
    return false;
  if (!FeasibilityResult->containsNode(I))
    return false;

  const auto ResMap = FeasibilityResult->resultsAt(I);
  auto It = ResMap.find(Zero);
  if (It == ResMap.end())
    return false;

  const auto &L = It->second;
  return !L.isBottom();
}

std::optional<Feasibility::FeasibilityAnalysis::l_t>
PhasarHandlerPass::getLatticeAtNodeForZero(const llvm::Instruction *I,
                                           const llvm::Value *Zero) const {
  if (!FeasibilityResult || !I || !Zero) return std::nullopt;
  if (!FeasibilityResult->containsNode(I)) return std::nullopt;

  const auto ResMap = FeasibilityResult->resultsAt(I);
  auto It = ResMap.find(Zero);
  if (It == ResMap.end()) return std::nullopt;

  return It->second;
}

const llvm::Instruction *
PhasarHandlerPass::pickSuccNodeFromICFG(const llvm::Instruction *PredTerm,
                                        const llvm::BasicBlock *SuccBB) const {
  if (!HA || !PredTerm || !SuccBB) return nullptr;

  const auto &ICFG = HA->getICFG();

  for (auto Succ : ICFG.getSuccsOf(PredTerm)) {
    if (Succ && Succ->getParent() == SuccBB) {
      return Succ;
    }
  }
  return nullptr;
}

bool PhasarHandlerPass::isEdgeFeasible(const llvm::BasicBlock *PredBB,
                                       const llvm::BasicBlock *SuccBB,
                                       const llvm::Value *Zero) const {
  if (!PredBB || !SuccBB || !Zero) return false;

  const llvm::Instruction *PredTerm = PredBB->getTerminator();
  if (!PredTerm) return false;

  // 1) Get L at predecessor terminator from IDE result
  auto LpredOpt = getLatticeAtNodeForZero(PredTerm, Zero);
  if (!LpredOpt) return false; // no result => treat as unreachable for reporting
  const auto &Lpred = *LpredOpt;

  // If predecessor is already infeasible, edge can't be feasible
  if (Lpred.isBottom())
    return false;

  // 2) Find the exact successor node used by the ICFG for this edge
  const llvm::Instruction *SuccNode = pickSuccNodeFromICFG(PredTerm, SuccBB);
  if (!SuccNode) {
    // If we can't identify the succ node, be conservative: don't prune
    return true;
  }

  // 3) Apply the analysis' own edge function for Zero along this edge
  // NOTE: Types depend on your feasibilityProblem type. For IDE problems, this is typical.
  auto EF = feasibilityProblem->getNormalEdgeFunction(
      const_cast<llvm::Instruction *>(PredTerm),
      const_cast<llvm::Value *>(Zero),
      const_cast<llvm::Instruction *>(SuccNode),
      const_cast<llvm::Value *>(Zero));

  auto Lsucc = EF.computeTarget(Lpred);

  return !Lsucc.isBottom();
}
