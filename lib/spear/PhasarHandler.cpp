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

#include <llvm/IR/Constants.h>
#include <llvm/IR/Dominators.h>
#include <llvm/IR/Operator.h>
#include <llvm/IR/Instructions.h>

#include <utility>
#include <string>
#include <memory>

#include "../../src/spear/analyses/loopbound/loopbound.h"
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

namespace {

// ---- Presence-aware query helpers (C++17 SFINAE) ----
template <typename T, typename = void> struct has_resultsAt : std::false_type {};

template <typename T>
struct has_resultsAt<T, std::void_t<decltype(std::declval<T &>().resultsAt(
                           (const llvm::Instruction *)nullptr))>>
    : std::true_type {};

template <typename T, typename = void>
struct has_getAllResultsAt : std::false_type {};

template <typename T>
struct has_getAllResultsAt<
    T, std::void_t<decltype(std::declval<T &>().getAllResultsAt(
           (const llvm::Instruction *)nullptr))>> : std::true_type {};

} // namespace

void PhasarHandlerPass::runAnalysis(llvm::FunctionAnalysisManager *FAM) {
  /*using ResultsTy =
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
  loopbound::LoopBoundIDEAnalysis Problem(&HA->getProjectIRDB(),
                                         &TopLoops);

  auto Result = psr::solveIDEProblem(Problem, ICFG);
  AnalysisResult = std::make_unique<ResultsTy>(std::move(Result));

  // --- Helpers (only keep the ones that require Problem/AnalysisResult) ---
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

  // --- NEW: print loop tests using util functions (no local lambdas) ---
  auto printLoopTests =
      [&](const loopbound::LoopDescription &LD, const llvm::Value *Root) {
        if (!LD.loop) {
          return;
        }

        llvm::SmallVector<llvm::BasicBlock *, 8> Exiting;
        LD.loop->getExitingBlocks(Exiting);

        if (Exiting.empty()) {
          llvm::errs() << "[LB] LoopTests: <no exiting blocks>\n";
          return;
        }

        llvm::errs() << "[LB] LoopTests (" << Exiting.size() << "):\n";

        for (llvm::BasicBlock *BB : Exiting) {
          if (!BB) {
            continue;
          }

          auto *TI = BB->getTerminator();
          auto *BI = llvm::dyn_cast_or_null<llvm::BranchInst>(TI);
          if (!BI || !BI->isConditional()) {
            llvm::errs() << "  - exiting BB " << BB->getName()
                         << ": <non-conditional terminator>\n";
            continue;
          }

          llvm::BasicBlock *T = BI->getSuccessor(0);
          llvm::BasicBlock *F = BI->getSuccessor(1);

          const bool TIn = T && LD.loop->contains(T);
          const bool FIn = F && LD.loop->contains(F);

          bool ExitOnTrue = false;
          if (T && !TIn) {
            ExitOnTrue = true;
          } else if (F && !FIn) {
            ExitOnTrue = false;
          }

          // util: peelToICmp
          const llvm::ICmpInst *Cmp = loopbound::peelToICmp(BI->getCondition());
          if (!Cmp) {
            llvm::errs() << "  - " << BB->getName()
                         << ": cond=" << *BI->getCondition()
                         << "  (no ICmp)\n";
            continue;
          }

          auto Pred = Cmp->getPredicate();
          if (!ExitOnTrue) {
            Pred = llvm::ICmpInst::getInversePredicate(Pred);
          }

          const llvm::Value *A = Cmp->getOperand(0);
          const llvm::Value *Bv = Cmp->getOperand(1);

          // util: getMemRootFromValue (pass stripAddr callable)
          const llvm::Value *CA = loopbound::getMemRootFromValue(A, stripAddr);
          const llvm::Value *CB = loopbound::getMemRootFromValue(Bv, stripAddr);

          const llvm::Value *CounterSide = nullptr;
          const llvm::Value *OtherSide = nullptr;

          if (CA && CA == Root) {
            CounterSide = A;
            OtherSide = Bv;
          } else if (CB && CB == Root) {
            CounterSide = Bv;
            OtherSide = A;
          } else {
            llvm::errs() << "  - exiting BB " << BB->getName() << " : ";
            llvm::errs() << "exit if (" << *Cmp << ")  [normalized pred="
                         << llvm::ICmpInst::getPredicateName(Pred) << "]\n";
            llvm::errs() << "    operands: A=" << *A << " , B=" << *Bv
                         << "  (counter side not identified)\n";
            continue;
          }

          llvm::errs() << "  - exiting BB " << BB->getName() << " : ";
          llvm::errs() << "exit if (" << *Cmp << ")  [normalized pred="
                       << llvm::ICmpInst::getPredicateName(Pred) << "]\n";

          llvm::errs() << "    counterSide: " << *CounterSide << "\n";
          llvm::errs() << "    otherSide  : " << *OtherSide << "\n";

          if (auto *C = llvm::dyn_cast<llvm::ConstantInt>(OtherSide)) {
            llvm::errs() << "    against const: " << C->getSExtValue() << "\n";
            continue;
          }

          const llvm::Value *CanonOther =
              loopbound::getMemRootFromValue(OtherSide, stripAddr);
          llvm::errs() << "    against canon: ";
          if (CanonOther) {
            llvm::errs() << *CanonOther << "\n";
          } else {
            llvm::errs() << "<null>\n";
          }

          // Dataflow interval (if present)
          const llvm::Instruction *AtI = llvm::cast<llvm::Instruction>(Cmp);
          if (CanonOther) {
            llvm::errs() << "    interval@" << BB->getName() << "(cmp): ";
            if (auto IV = tryGet(AtI, CanonOther)) {
              llvm::errs() << *IV << "\n";
            } else {
              llvm::errs() << "<no fact>\n";
            }
          }

          // util: IR-based constant deduction (no local lambda)
          if (auto *LI = llvm::dyn_cast<llvm::LoadInst>(OtherSide)) {
            llvm::Function *Fn = const_cast<llvm::Function *>(LI->getFunction());
            if (Fn) {
              auto &DT = FAM->getResult<llvm::DominatorTreeAnalysis>(*Fn);
              if (auto Cst = loopbound::tryDeduceConstFromLoad(LI, DT)) {
                llvm::errs() << "    deduce const: " << *Cst << "\n";
              } else {
                llvm::errs() << "    deduce: <unknown>\n";
              }
            }
          }
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

    printLoopTests(LD, Root);

    if (!IncStore) {
      llvm::errs() << "[LB] IncStore: <not found>\n";
      llvm::errs() << "[LB] ==========================\n";
      continue;
    }

    printAt("IncStore", IncStore, Root);
    printAt("AfterInc", AfterInc, Root);

    llvm::errs() << "[LB] ==========================\n";
  }*/

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

  using DomainVal = loopbound::DeltaInterval;

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
