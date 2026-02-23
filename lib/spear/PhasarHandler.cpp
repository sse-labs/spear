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

bool PhasarHandlerPass::constains(std::vector<llvm::BasicBlock *> visited, llvm::BasicBlock *BB) const {
  for (auto *V : visited) {
    if (V == BB) {
      return true;
    }
  }
  return false;
}

PhasarHandlerPass::FeasibilityMap
PhasarHandlerPass::queryFeasibility(llvm::Function *Func) const {
  FeasibilityMap ResultMap;

  if (!FeasibilityResult || !Func) {
    return ResultMap;
  }

  const llvm::Value *Zero = feasibilityProblem ? feasibilityProblem->getZeroValue() : nullptr;

  struct SetSigHash {
    size_t operator()(const SetSigKey &K) const noexcept {
      // Same idea as DenseMap hash, but return size_t
      return (size_t)llvm::hash_combine(K.Mgr,
        llvm::hash_combine_range(K.AstIds.begin(), K.AstIds.end()));
    }
  };

  // Create the worklist and visited set for a simple CFG traversal to query feasibility at block entries.
  auto firstBlock = &Func->getEntryBlock();
  std::deque<llvm::BasicBlock*> worklist{firstBlock};
  llvm::DenseMap<const llvm::BasicBlock*, BlockFeasInfo> visited;
  std::unordered_map<SetSigKey, bool, SetSigHash> SatCache;
  SatCache.reserve(128); // optional

  // At all blocks to the visited set with default entries to ensure we don't revisit them. We will fill in the actual feasibility info in the next loop.
  for (auto &BB : *Func) {
    const std::string BBName = blockName(BB);
    visited[&BB] = BlockFeasInfo{}; // default entry
  }

  // Iterate over the worklist until its empty
  while (!worklist.empty()) {
    // Get the first entry
    llvm::BasicBlock *BB = worklist.front();
    worklist.pop_front();

    // Check if we have already visited this block. If so, skip it.
    const std::string BBName = blockName(*BB);

    // llvm::errs() << "Solving for " << BBName << "\n";

    if (visited[BB].visited) {
      continue; // already visited
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

        const auto *Mgr = entry.getManager();
        uint32_t FId = entry.getFormulaId();

        std::vector<z3::expr> set = Mgr->getPureSet(FId);
        SetSigKey Sig = makeSig(Mgr, set);

        auto ItSat = SatCache.find(Sig);
        bool isSat;
        if (ItSat != SatCache.end()) {
          isSat = ItSat->second;
        } else {
          isSat = Feasibility::Util::setSat(set, Mgr->Context.get());
          SatCache.emplace(std::move(Sig), isSat);
        }

        ResultMap[BBName].Feasible = isSat;
        ResultMap[BBName].HasZeroAtEntry = true;
        ResultMap[BBName].visited = true;

        if (isSat) {
          auto sucss = llvm::successors(BB);
          worklist.insert(worklist.end(), sucss.begin(), sucss.end());
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
