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
    if (!Ptr) return nullptr;
    return Problem.stripAddr(Ptr);
  };

  // Find the store that matches "i = i + C" for this loop's counterRoot
  auto findIncStoreInLoop =
      [&](const loopbound::LoopDescription &LD) -> const llvm::StoreInst * {
        if (!LD.loop || !LD.counterRoot) return nullptr;

        const llvm::Value *Root = stripAddr(LD.counterRoot);
        if (!Root) return nullptr;

        for (llvm::BasicBlock *BB : LD.loop->blocks()) {
          for (llvm::Instruction &I : *BB) {
            auto *SI = llvm::dyn_cast<llvm::StoreInst>(&I);
            if (!SI) continue;
            if (Problem.extractConstIncFromStore(SI, Root).has_value()) {
              return SI;
            }
          }
        }
        return nullptr;
      };

  // SAFE query: avoid resultAt() unless the (stmt,fact) entry exists.
  auto safeResultAt =
      [&](const llvm::Instruction *I, const llvm::Value *Fact)
          -> std::optional<loopbound::DeltaInterval> {
        if (!I || !Fact) return std::nullopt;

        const llvm::Value *CanonFact = stripAddr(Fact);
        if (!CanonFact) return std::nullopt;

        // resultsAt(I) is safe; we just scan for the fact.
        // (This avoids SIGSEGV when the pair isn't in the map.)
        auto Map = AnalysisResult->resultsAt(I);
        auto It = Map.find(CanonFact);
        if (It == Map.end()) return std::nullopt;

        return It->second;
      };

  // --- Print the collected interval where it actually exists: at the inc store ---
  const auto LoopDescs = Problem.getLoopDescriptions();

  for (const auto &LD : LoopDescs) {
    if (!LD.loop || !LD.counterRoot) continue;

    const llvm::Function *F = LD.loop->getHeader()->getParent();
    if (!F || F->getName() != "main") continue;

    const llvm::Value *Root = stripAddr(LD.counterRoot);
    if (!Root) continue;

    const llvm::StoreInst *IncStore = findIncStoreInLoop(LD);

    llvm::errs() << "\n[LB] === Loop Result (main) ===\n";
    llvm::errs() << "[LB] Loop: " << LD.loop->getName() << "\n";
    llvm::errs() << "[LB] Counter root: " << *Root << "\n";

    if (!IncStore) {
      llvm::errs() << "[LB] IncStore: <not found>\n";
      llvm::errs() << "[LB] ==========================\n";
      continue;
    }

    llvm::errs() << "[LB] IncStore: " << *IncStore << "\n";

    const llvm::Instruction *AfterInc = succOf(IncStore);

    auto VPre  = safeResultAt(IncStore, Root);
    auto VPost = safeResultAt(AfterInc, Root);

    llvm::errs() << "[LB] Value@IncStore: ";
    if (VPre) llvm::errs() << *VPre << "\n"; else llvm::errs() << "<absent>\n";

    llvm::errs() << "[LB] AfterInc: ";
    if (AfterInc) llvm::errs() << *AfterInc << "\n"; else llvm::errs() << "<null>\n";

    llvm::errs() << "[LB] Value@AfterInc: ";
    if (VPost) llvm::errs() << *VPost << "\n"; else llvm::errs() << "<absent>\n";

    if (auto V = safeResultAt(IncStore, Root)) {
      llvm::errs() << "[LB] Value@IncStore(pre): " << *V << "\n";
    } else {
      llvm::errs() << "[LB] Value@IncStore(pre): <absent at this stmt>\n";
      llvm::errs() << "[LB] Hint: fact may be killed by a flow function on some path.\n";
    }

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
