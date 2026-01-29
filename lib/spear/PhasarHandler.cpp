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

  auto &FAM = AM.getResult<llvm::FunctionAnalysisManagerModuleProxy>(*mod).getManager();
  runAnalysis(&FAM);

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

static llvm::SmallVector<const llvm::Instruction*, 4>
succInsts(const llvm::Instruction *I) {
  llvm::SmallVector<const llvm::Instruction*, 4> out;
  if (!I) return out;

  if (auto *N = I->getNextNode()) {
    out.push_back(N);
    return out;
  }

  const auto *B = I->getParent();
  if (!B) return out;

  const auto *T = B->getTerminator();
  if (!T) return out;

  for (const llvm::BasicBlock *SB : llvm::successors(B)) {
    if (!SB || SB->empty()) continue;
    out.push_back(&*SB->begin());
  }
  return out;
}

const llvm::Instruction *succOf(const llvm::Instruction *I) {
  if (!I) return nullptr;

  // same basic block successor (most common: store -> br / next inst)
  if (const llvm::Instruction *N = I->getNextNode()) {
    return N;
  }

  // if it's the terminator, follow CFG edge to successor block's first inst
  const llvm::BasicBlock *BB = I->getParent();
  if (!BB) return nullptr;

  const llvm::Instruction *Term = BB->getTerminator();
  if (Term != I) return nullptr;

  if (const auto *BI = llvm::dyn_cast<llvm::BranchInst>(Term)) {
    if (BI->getNumSuccessors() > 0) {
      const llvm::BasicBlock *S = BI->getSuccessor(0);
      if (S && !S->empty()) return &*S->begin();
    }
  } else if (const auto *SI = llvm::dyn_cast<llvm::SwitchInst>(Term)) {
    const llvm::BasicBlock *S = SI->getDefaultDest();
    if (S && !S->empty()) return &*S->begin();
  }

  return nullptr;
}

namespace {

// ---- Presence-aware query helpers (C++17 SFINAE) ----
template <typename T, typename = void>
struct has_resultsAt : std::false_type {};

template <typename T>
struct has_resultsAt<T, std::void_t<decltype(std::declval<T &>().resultsAt(
                         (const llvm::Instruction *)nullptr))>> : std::true_type {};

template <typename T, typename = void>
struct has_getAllResultsAt : std::false_type {};

template <typename T>
struct has_getAllResultsAt<T, std::void_t<decltype(std::declval<T &>().getAllResultsAt(
                             (const llvm::Instruction *)nullptr))>> : std::true_type {};

} // namespace


void PhasarHandlerPass::runAnalysis(llvm::FunctionAnalysisManager *FAM) {
  using ResultsTy =
      psr::OwningSolverResults<const llvm::Instruction *, const llvm::Value *,
                               loopbound::DeltaInterval>;

  auto &ICFG = HA->getICFG();

  llvm::Function *Main = HA->getProjectIRDB().getFunctionDefinition("main");
  if (!Main) {
    llvm::errs() << "[LB] main not found\n";
    return;
  }

  // --- Collect top-level loops in main ---
  auto &LoopInfo = FAM->getResult<llvm::LoopAnalysis>(*Main);

  std::vector<llvm::Loop *> TopLoops;
  TopLoops.reserve(LoopInfo.getTopLevelLoopsVector().size());
  for (llvm::Loop *L : LoopInfo) {
    if (L && !L->getParentLoop()) {
      TopLoops.push_back(L);
    }
  }

  // --- Construct & solve IDE problem ---
  loopbound::LoopBoundIDEAnalysis Problem(&HA->getProjectIRDB(), Entrypoints,
                                         &TopLoops);

  auto Result = psr::solveIDEProblem(Problem, ICFG);
  AnalysisResult = std::make_unique<ResultsTy>(std::move(Result));

  // --- Helpers ---
  auto stripAddr = [&](const llvm::Value *Ptr) -> const llvm::Value * {
    if (!Ptr) {
      return nullptr;
    }
    return Problem.stripAddr(Ptr);
  };

  auto findIncStoreInLoop =
      [&](const loopbound::LoopDescription &LD) -> const llvm::StoreInst * {
    if (!LD.loop || !LD.counterRoot) {
      return nullptr;
    }

    const llvm::Value *Root = stripAddr(LD.counterRoot);
    if (!Root) {
      return nullptr;
    }

    for (llvm::BasicBlock *BB : LD.loop->blocks()) {
      for (llvm::Instruction &I : *BB) {
        auto *SI = llvm::dyn_cast<llvm::StoreInst>(&I);
        if (!SI) {
          continue;
        }
        if (Problem.extractConstIncFromStore(SI, Root).has_value()) {
          return SI;
        }
      }
    }
    return nullptr;
  };

  // successor node after store (where the store-edge EF has been applied)
  auto succAfterStore =
      [&](const llvm::StoreInst *SI) -> const llvm::Instruction * {
    if (!SI) {
      return nullptr;
    }
    if (const llvm::Instruction *N = SI->getNextNode()) {
      return N;
    }
    return SI->getParent() ? SI->getParent()->getTerminator() : nullptr;
  };

  // Presence-aware "tryGet": distinguishes "<no fact>" vs "⊤" when possible.
  auto tryGet = [&](const llvm::Instruction *I, const llvm::Value *Fact)
      -> std::optional<loopbound::DeltaInterval> {
    if (!I || !Fact || !AnalysisResult) {
      return std::nullopt;
    }

    const llvm::Value *Canon = stripAddr(Fact);
    if (!Canon) {
      return std::nullopt;
    }

     if constexpr (has_resultsAt<ResultsTy>::value) {
      const auto &Map = AnalysisResult->resultsAt(I);
      auto It = Map.find(Canon);
      if (It == Map.end()) {
        return std::nullopt;
      }
      return It->second;

    } else {
      // fallback: can't distinguish "missing" vs TOP in this API shape
      return AnalysisResult->resultAt(I, Canon);
    }
  };

  auto printAt = [&](const char *Label, const llvm::Instruction *I,
                     const llvm::Value *Fact) {
    llvm::errs() << "[LB] " << Label << ": ";
    if (!I) {
      llvm::errs() << "<null>\n";
      llvm::errs() << "[LB] Value@" << Label << "(pre): <no inst>\n";
      return;
    }
    llvm::errs() << *I << "\n";

    llvm::errs() << "[LB] Value@" << Label << "(pre): ";
    if (auto V = tryGet(I, Fact)) {
      llvm::errs() << *V << "\n";
    } else {
      llvm::errs() << "<no fact>\n";
    }
  };

  // --- Iterate loop descriptions and print values at store + after-store ---
  const auto LoopDescs = Problem.getLoopDescriptions();

  for (const auto &LD : LoopDescs) {
    if (!LD.loop || !LD.counterRoot) {
      continue;
    }

    const llvm::Function *F = LD.loop->getHeader()->getParent();
    if (!F || F->getName() != "main") {
      continue;
    }

    const llvm::Value *Root = stripAddr(LD.counterRoot);
    if (!Root) {
      continue;
    }

    const llvm::StoreInst *IncStore = findIncStoreInLoop(LD);
    const llvm::Instruction *AfterInc = succAfterStore(IncStore);

    llvm::errs() << "\n[LB] === Loop Result (main) ===\n";
    llvm::errs() << "[LB] Loop: " << LD.loop->getName() << "\n";
    llvm::errs() << "[LB] Counter root: " << *Root << "\n";

    if (!IncStore) {
      llvm::errs() << "[LB] IncStore: <not found>\n";
      llvm::errs() << "[LB] ==========================\n";
      continue;
    }

    llvm::errs() << "[LB] IncStore: " << *IncStore << "\n";

    // PRE at store
    printAt("IncStore", IncStore, Root);

    // PRE at successor (this is where COLLECT should show up as value)
    printAt("AfterInc", AfterInc, Root);

    llvm::errs() << "[LB] ==========================\n";
  }
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
