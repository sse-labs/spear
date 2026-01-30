/*
 * Copyright (c) 2026 Maximilian Krebs
 * All rights reserved.
*/

#include "analyses/loopbound/LoopBound.h"

#include <phasar/PhasarLLVM/ControlFlow/LLVMBasedCFG.h>

#include <memory>
#include <utility>
#include <llvm/IR/Operator.h>
#include <phasar/PhasarLLVM/DataFlow/IfdsIde/LLVMZeroValue.h>

#include "analyses/loopbound/LoopBoundEdgeFunction.h"
#include "analyses/loopbound/util.h"

#include <atomic>

namespace {

template <typename D, typename ContainerT>
class DebugFlow final : public psr::FlowFunction<D, ContainerT> {
  std::shared_ptr<psr::FlowFunction<D, ContainerT>> Inner;
  const char *Name;
  LoopBound::LoopBoundIDEAnalysis *A;
  LoopBound::LoopBoundIDEAnalysis::n_t Curr;
  LoopBound::LoopBoundIDEAnalysis::n_t Succ;

public:
  DebugFlow(std::shared_ptr<psr::FlowFunction<D, ContainerT>> Inner,
            const char *Name,
            LoopBound::LoopBoundIDEAnalysis *A,
            LoopBound::LoopBoundIDEAnalysis::n_t Curr,
            LoopBound::LoopBoundIDEAnalysis::n_t Succ)
      : Inner(std::move(Inner)), Name(Name), A(A), Curr(Curr), Succ(Succ) {}

  ContainerT computeTargets(D Src) override {
    ContainerT Out = Inner->computeTargets(Src);

    if (LoopBound::Util::LB_DebugEnabled.load()) {
      llvm::errs() << LoopBound::Util::LB_TAG << " FF " << Name << "  ";
      LoopBound::Util::dumpInst(Curr);
      llvm::errs() << "  ->  ";
      LoopBound::Util::dumpInst(Succ);
      llvm::errs() << "\n" << LoopBound::Util::LB_TAG << "   Src=";
      LoopBound::Util::dumpFact(A,
                          static_cast<LoopBound::LoopBoundIDEAnalysis::d_t>(Src));
      llvm::errs() << "   Targets={";
      bool first = true;
      for (auto T : Out) {
        if (!first) {
          llvm::errs() << ", ";
        }
        first = false;
        LoopBound::Util::dumpFact(
            A, static_cast<LoopBound::LoopBoundIDEAnalysis::d_t>(T));
      }
      llvm::errs() << "}\n";
    }

    return Out;
  }
};

template <typename D, typename ContainerT>
class IdentityFlow final : public psr::FlowFunction<D, ContainerT> {
public:
  ContainerT computeTargets(D Src) override { return ContainerT{Src}; }
};


template <typename D, typename ContainerT>
class KeepLocalOnCallToRet final : public psr::FlowFunction<D, ContainerT> {
public:
  ContainerT computeTargets(D Src) override { return ContainerT{Src}; }
};

} // namespace

namespace LoopBound {

// ======================= LoopBoundIDEAnalysis =======================

LoopBoundIDEAnalysis::LoopBoundIDEAnalysis(const psr::LLVMProjectIRDB *IRDB,
                                           std::vector<llvm::Loop *> *loops)
    : base_t(IRDB, {"main"},
             std::optional<d_t>(
                 static_cast<d_t>(psr::LLVMZeroValue::getInstance()))) {
  this->loops = loops;
  this->findLoopCounters();
}

// === Instrument initialSeeds() ===
psr::InitialSeeds<LoopBoundIDEAnalysis::n_t, LoopBoundIDEAnalysis::d_t,
                  LoopBoundIDEAnalysis::l_t>
LoopBoundIDEAnalysis::initialSeeds() {
  psr::InitialSeeds<n_t, d_t, l_t> Seeds;

  for (auto &desc : LoopDescriptions) {
    llvm::Loop *loop = desc.loop;
    const llvm::Value *root = desc.counterRoot;

    if (!loop || !root) {
      continue;
    }

    llvm::BasicBlock *header = loop->getHeader();
    if (!header || header->empty()) {
      continue;
    }

    n_t headerNode = &*header->begin();

    const llvm::Value *CanonRoot = LoopBound::Util::stripAddr(root);

    llvm::errs() << LoopBound::Util::LB_TAG << " Seed @header=";
    LoopBound::Util::dumpInst(headerNode);
    llvm::errs() << "\n" << LoopBound::Util::LB_TAG << "   Zero=";
    LoopBound::Util::dumpFact(this, getZeroValue());
    llvm::errs() << "  -> " << l_t::ideNeutral() << "\n";

    llvm::errs() << LoopBound::Util::LB_TAG << "   Root=" << CanonRoot
                 << " -> " << l_t::ideNeutral() << "\n";

    Seeds.addSeed(headerNode, getZeroValue(), topElement());
    Seeds.addSeed(headerNode, static_cast<d_t>(CanonRoot), l_t::empty());
  }

  return Seeds;
}

bool LoopBoundIDEAnalysis::isZeroValue(d_t Fact) const noexcept {
  return base_t::isZeroValue(Fact);
}

LoopBoundIDEAnalysis::l_t LoopBoundIDEAnalysis::topElement() {
  return l_t::ideNeutral();
}

LoopBoundIDEAnalysis::l_t LoopBoundIDEAnalysis::bottomElement() {
  return l_t::ideAbsorbing();
}

LoopBoundIDEAnalysis::l_t LoopBoundIDEAnalysis::join(l_t Lhs, l_t Rhs) {
  l_t Res;

  if (Lhs.isBottom()) {
    Res = Rhs;
  } else if (Rhs.isBottom()) {
    Res = Lhs;
  } else if (Lhs.isIdeAbsorbing() || Rhs.isIdeAbsorbing()) {
    Res = l_t::ideAbsorbing();
  } else if (Lhs.isIdeNeutral()) {
    Res = Rhs;
  } else if (Rhs.isIdeNeutral()) {
    Res = Lhs;
  } else {
    Res = Lhs.leastUpperBound(Rhs);
  }

  if (LoopBound::Util::LB_DebugEnabled.load()) {
    llvm::errs() << "[LB] join: " << Lhs << " lub " << Rhs << " = " << Res << "\n";
  }
  return Res;
}

psr::EdgeFunction<LoopBoundIDEAnalysis::l_t>
LoopBoundIDEAnalysis::allTopFunction() {
  return psr::AllTop<l_t>{};
}

// ---------------- Flow functions ----------------

// Detect latch->header (backedge) for one of our tracked loops.
bool LoopBoundIDEAnalysis::isLatchToHeaderEdge(n_t Curr, n_t Succ) const {
  if (!Curr || !Succ) {
    return false;
  }

  const llvm::BasicBlock *CB = Curr->getParent();
  const llvm::BasicBlock *SB = Succ->getParent();
  if (!CB || !SB) {
    return false;
  }

  // Succ must be the first instruction of some loop header we track.
  if (&*SB->begin() != Succ) {
    return false;
  }

  for (const auto &LD : LoopDescriptions) {
    llvm::Loop *L = LD.loop;
    if (!L) {
      continue;
    }

    llvm::BasicBlock *Header = L->getHeader();
    if (!Header) {
      continue;
    }

    if (SB != Header) {
      continue;
    }

    // Edge originates inside the loop, from a latch, to header.
    if (!L->contains(CB)) {
      continue;
    }
    if (CB == Header) {
      continue;
    }

    if (!L->isLoopLatch(const_cast<llvm::BasicBlock *>(CB))) {
      continue;
    }

    // Also ensure CB actually has successor Header (cheap sanity check)
    const llvm::Instruction *Term = CB->getTerminator();
    if (!Term) {
      continue;
    }
    bool HasHeaderSucc = false;
    for (const llvm::BasicBlock *S : llvm::successors(CB)) {
      if (S == Header) {
        HasHeaderSucc = true;
        break;
      }
    }
    if (!HasHeaderSucc) {
      continue;
    }

    return true;
  }

  return false;
}

LoopBoundIDEAnalysis::FlowFunctionPtrType
LoopBoundIDEAnalysis::getNormalFlowFunction(n_t Curr, n_t Succ) {
  if (isLatchToHeaderEdge(Curr, Succ)) {
    if (LoopBound::Util::LB_DebugEnabled.load()) {
      llvm::errs() << "[LB] CUT edge (no-kill facts): " << *Curr << " -> " << *Succ << "\n";
    }
  }

  auto Inner = std::make_shared<IdentityFlow<d_t, container_t>>();
  return std::make_shared<DebugFlow<d_t, container_t>>(Inner, "Identity", this, Curr, Succ);
}


LoopBoundIDEAnalysis::FlowFunctionPtrType
LoopBoundIDEAnalysis::getCallFlowFunction(n_t Curr, f_t) {
  auto Inner = std::make_shared<IdentityFlow<d_t, container_t>>();
  return std::make_shared<DebugFlow<d_t, container_t>>(Inner, "CallIdentity", this, Curr, Curr);
}

LoopBoundIDEAnalysis::FlowFunctionPtrType
LoopBoundIDEAnalysis::getRetFlowFunction(n_t Curr, f_t, n_t, n_t) {
  auto Inner = std::make_shared<IdentityFlow<d_t, container_t>>();
  return std::make_shared<DebugFlow<d_t, container_t>>(Inner, "RetIdentity", this, Curr, Curr);
}

LoopBoundIDEAnalysis::FlowFunctionPtrType
LoopBoundIDEAnalysis::getCallToRetFlowFunction(n_t Curr, n_t Succ,
                                               llvm::ArrayRef<f_t>) {
  auto Inner = std::make_shared<KeepLocalOnCallToRet<d_t, container_t>>();
  return std::make_shared<DebugFlow<d_t, container_t>>(Inner,
                                                      "CallToRetKeepLocal",
                                                      this, Curr, Succ);
}

// ---------------- Edge functions ----------------

// Helper: determine which LoopDescription applies at a program point (curr instruction).
const LoopParameterDescription *
LoopBoundIDEAnalysis::getLoopDescriptionForInst(const llvm::Instruction *inst) const {
  if (!inst) {
    return nullptr;
  }
  for (const auto &LD : LoopDescriptions) {
    if (!LD.loop || !LD.counterRoot) {
      continue;
    }
    if (LD.loop->contains(inst)) {
      return &LD;
    }
  }
  return nullptr;
}

bool LoopBoundIDEAnalysis::isCounterRootFactAtInst(d_t Fact, n_t AtInst) const {
  if (!Fact || isZeroValue(Fact) || !AtInst) {
    return false;
  }

  const auto *V = static_cast<const llvm::Value *>(Fact);
  V = LoopBound::Util::stripAddr(V);

  const LoopParameterDescription *LD = getLoopDescriptionForInst(AtInst);
  if (!LD || !LD->loop) {
    return false;
  }

  const llvm::Value *Root = LoopBound::Util::stripAddr(LD->counterRoot);

  // Also guard by function (stack allocas are function-local)
  const llvm::Function *F1 = AtInst->getFunction();
  const llvm::Function *F2 = nullptr;
  if (auto *RI = llvm::dyn_cast<llvm::Instruction>(Root)) {
    F2 = RI->getFunction();
  } else if (auto *RA = llvm::dyn_cast<llvm::AllocaInst>(Root)) {
    F2 = RA->getFunction();
  }
  if (F2 && F1 != F2) {
    return false;
  }

  return V == Root;
}


// === Instrument getNormalEdgeFunction() heavily ===
LoopBoundIDEAnalysis::EdgeFunctionType
LoopBoundIDEAnalysis::getNormalEdgeFunction(n_t curr, d_t currNode, n_t Succ,
                                            d_t succNode) {

  if (isLatchToHeaderEdge(curr, Succ)) {
    return EF(std::in_place_type<DeltaIntervalIdentity>);
  }

  if (LoopBound::Util::LB_DebugEnabled.load()) {
    llvm::errs() << LoopBound::Util::LB_TAG << " EF normal @";
    LoopBound::Util::dumpInst(curr);
    llvm::errs() << "\n" << LoopBound::Util::LB_TAG << "   currFact=";
    LoopBound::Util::dumpFact(this, currNode);
    llvm::errs() << "\n" << LoopBound::Util::LB_TAG << "   succFact=";
    LoopBound::Util::dumpFact(this, succNode);
    llvm::errs() << "\n";
  }

  if (isZeroValue(currNode) || isZeroValue(succNode)) {
    auto E = EF(std::in_place_type<DeltaIntervalIdentity>);
    if (LoopBound::Util::LB_DebugEnabled.load()) {
      llvm::errs() << LoopBound::Util::LB_TAG << "   reason=zero-involved  ";
      LoopBound::Util::dumpEF(E);
      llvm::errs() << "\n";
    }
    return E;
  }

  if (currNode != succNode) {
    auto E = EF(std::in_place_type<DeltaIntervalIdentity>);
    if (LoopBound::Util::LB_DebugEnabled.load()) {
      llvm::errs() << LoopBound::Util::LB_TAG << "   reason=fact-changed(curr!=succ)  ";
      LoopBound::Util::dumpEF(E);
      llvm::errs() << "\n";
    }
    return E;
  }

  const bool IsRootHere = isCounterRootFactAtInst(currNode, curr);
  if (LoopBound::Util::LB_DebugEnabled.load()) {
    llvm::errs() << LoopBound::Util::LB_TAG << "   isCounterRootFactAtInst="
                 << (IsRootHere ? "true" : "false") << "\n";
  }

  if (!IsRootHere) {
    auto E = EF(std::in_place_type<DeltaIntervalIdentity>);
    if (LoopBound::Util::LB_DebugEnabled.load()) {
      llvm::errs() << LoopBound::Util::LB_TAG << "   reason=not-root-fact  ";
      LoopBound::Util::dumpEF(E);
      llvm::errs() << "\n";
    }
    return E;
  }

  if (auto *storeInst = llvm::dyn_cast<llvm::StoreInst>(curr)) {
    const llvm::Value *root = LoopBound::Util::stripAddr(static_cast<const llvm::Value *>(currNode));

    if (auto increment = extractConstIncFromStore(storeInst, root)) {
      auto E = EF(std::in_place_type<DeltaIntervalCollect>,
                  static_cast<int64_t>(*increment),
                  static_cast<int64_t>(*increment));

      if (LoopBound::Util::LB_DebugEnabled.load()) {
        llvm::errs() << "[LB] INC matched at store: " << *storeInst
                     << "  inc=" << *increment << "\n";
        llvm::errs() << LoopBound::Util::LB_TAG << "   produced ";
        LoopBound::Util::dumpEF(E);
        llvm::errs() << "\n";
      }
      return E;
    }
  }

  auto E = EF(std::in_place_type<DeltaIntervalIdentity>);
  if (LoopBound::Util::LB_DebugEnabled.load()) {
    llvm::errs() << LoopBound::Util::LB_TAG << "   reason=no-inc  ";
    LoopBound::Util::dumpEF(E);
    llvm::errs() << "\n";
  }
  return E;
}

LoopBoundIDEAnalysis::EdgeFunctionType
LoopBoundIDEAnalysis::getCallEdgeFunction(n_t, d_t, f_t, d_t) {
  return EF(std::in_place_type<DeltaIntervalIdentity>);
}

LoopBoundIDEAnalysis::EdgeFunctionType
LoopBoundIDEAnalysis::getReturnEdgeFunction(n_t, f_t, n_t, d_t, n_t, d_t) {
  return EF(std::in_place_type<DeltaIntervalIdentity>);
}

LoopBoundIDEAnalysis::EdgeFunctionType
LoopBoundIDEAnalysis::getCallToRetEdgeFunction(n_t, d_t, n_t, d_t,
                                               llvm::ArrayRef<f_t>) {
  return EF(std::in_place_type<DeltaIntervalIdentity>);
}

// ---------------- Existing helpers ----------------

std::optional<int64_t>
LoopBoundIDEAnalysis::extractConstIncFromStore(const llvm::StoreInst *storeInst,
                                              const llvm::Value *counterRoot) {
  if (!storeInst || !counterRoot) {
    return std::nullopt;
  }

  const llvm::Value *destination = LoopBound::Util::stripAddr(storeInst->getPointerOperand());
  const llvm::Value *root        = LoopBound::Util::stripAddr(counterRoot);

  if (LoopBound::Util::LB_DebugEnabled.load()) {
    llvm::errs() << LoopBound::Util::LB_TAG << " extractConstIncFromStore\n";
    llvm::errs() << LoopBound::Util::LB_TAG << "   store=" << *storeInst << "\n";
    llvm::errs() << LoopBound::Util::LB_TAG << "   dst=" << destination << "  root=" << root << "\n";
  }

  if (destination != root) {
    if (LoopBound::Util::LB_DebugEnabled.load()) {
      llvm::errs() << LoopBound::Util::LB_TAG << "   FAIL: destination!=root\n";
    }
    return std::nullopt;
  }

  const llvm::Value *value = storeInst->getValueOperand();
  auto *binaryOperator = llvm::dyn_cast<llvm::BinaryOperator>(value);
  if (!binaryOperator) {
    if (LoopBound::Util::LB_DebugEnabled.load()) {
      llvm::errs() << LoopBound::Util::LB_TAG
                   << "   FAIL: value not BinaryOperator, value=" << *value
                   << "\n";
    }
    return std::nullopt;
  }

  llvm::Value *firstOperand  = binaryOperator->getOperand(0);
  llvm::Value *secondOperand = binaryOperator->getOperand(1);

  if (LoopBound::Util::LB_DebugEnabled.load()) {
    llvm::errs() << LoopBound::Util::LB_TAG << "   binop=" << *binaryOperator << "\n";
  }

  switch (binaryOperator->getOpcode()) {
    case llvm::Instruction::Add: {
      if (isLoadOfCounterRoot(firstOperand, root)) {
        if (auto *constantIncrement =
                llvm::dyn_cast<llvm::ConstantInt>(secondOperand)) {
          if (LoopBound::Util::LB_DebugEnabled.load()) {
            llvm::errs() << LoopBound::Util::LB_TAG << "   OK: load(root)+C  C="
                         << constantIncrement->getSExtValue() << "\n";
          }
          return constantIncrement->getSExtValue();
        }
      }

      if (isLoadOfCounterRoot(secondOperand, root)) {
        if (auto *constantIncrement =
                llvm::dyn_cast<llvm::ConstantInt>(firstOperand)) {
          if (LoopBound::Util::LB_DebugEnabled.load()) {
            llvm::errs() << LoopBound::Util::LB_TAG << "   OK: C+load(root)  C="
                         << constantIncrement->getSExtValue() << "\n";
          }
          return constantIncrement->getSExtValue();
        }
      }

      if (LoopBound::Util::LB_DebugEnabled.load()) {
        llvm::errs() << LoopBound::Util::LB_TAG << "   FAIL: add but not (load(root)+C)\n";
      }
      return std::nullopt;
    }

    case llvm::Instruction::Sub: {
      if (isLoadOfCounterRoot(firstOperand, root)) {
        if (auto *constantIncrement =
                llvm::dyn_cast<llvm::ConstantInt>(secondOperand)) {
          if (LoopBound::Util::LB_DebugEnabled.load()) {
            llvm::errs() << LoopBound::Util::LB_TAG << "   OK: load(root)-C  C="
                         << constantIncrement->getSExtValue() << "\n";
          }
          return -constantIncrement->getSExtValue();
        }
      }

      if (LoopBound::Util::LB_DebugEnabled.load()) {
        llvm::errs() << LoopBound::Util::LB_TAG << "   FAIL: sub but not (load(root)-C)\n";
      }
      return std::nullopt;
    }

    default: {
      if (LoopBound::Util::LB_DebugEnabled.load()) {
        llvm::errs() << LoopBound::Util::LB_TAG << "   FAIL: opcode not add/sub\n";
      }
      return std::nullopt;
    }
  }
}


bool LoopBoundIDEAnalysis::isLoadOfCounterRoot(llvm::Value *value,
                                              const llvm::Value *root) {
  if (auto *loopInstruction = llvm::dyn_cast<llvm::LoadInst>(value)) {
    const llvm::Value *ptr = LoopBound::Util::stripAddr(loopInstruction->getPointerOperand());
    return ptr == root;
  }
  return false;
}

std::optional<int64_t>
LoopBoundIDEAnalysis::findConstInitForCell(const llvm::Value *Addr,
                                          llvm::Loop *L) {
  auto strippedAddr = LoopBound::Util::stripAddr(Addr);
  llvm::BasicBlock *PreH = L->getLoopPreheader();
  if (!PreH) return std::nullopt;

  for (llvm::Instruction &I : *PreH) {
    auto *SI = llvm::dyn_cast<llvm::StoreInst>(&I);
    if (!SI) continue;
    if (LoopBound::Util::stripAddr(SI->getPointerOperand()) != strippedAddr) continue;

    if (auto *CI = llvm::dyn_cast<llvm::ConstantInt>(SI->getValueOperand()))
      return CI->getSExtValue();
  }
  return std::nullopt;
}

void LoopBoundIDEAnalysis::findLoopCounters() {
  for (auto loop : *this->loops) {

    llvm::SmallVector<llvm::BasicBlock *, 8> ExitingBlocks;
    loop->getExitingBlocks(ExitingBlocks);

    for (llvm::BasicBlock *EB : ExitingBlocks) {
      auto *br = llvm::dyn_cast<llvm::BranchInst>(EB->getTerminator());
      if (!br || !br->isConditional()) continue;

      llvm::Value *cond = br->getCondition();
      auto *icmp = llvm::dyn_cast<llvm::ICmpInst>(cond);
      if (!icmp) continue;

      auto info = findCounterFromICMP(icmp, loop);
      if (!info || info->Roots.empty()) continue;

      auto init = findConstInitForCell(info->Roots[0], loop);

      llvm::Function *LoopF = loop->getHeader()->getParent();

      auto *RootI = llvm::dyn_cast<llvm::Instruction>(info->Roots[0]);
      if (RootI && RootI->getFunction() != LoopF) continue;

      auto *RootA = llvm::dyn_cast<llvm::AllocaInst>(info->Roots[0]);
      if (RootA && RootA->getFunction() != LoopF) continue;

      LoopParameterDescription description = {
          loop,
          icmp,
          info->Roots[0],
          init,
      };

      LoopDescriptions.push_back(description);

      llvm::errs() << "Generated Loop Description {\n";
      llvm::errs() << "Loop: " << description.loop->getName() << "\n";
      llvm::errs() << "ICMP: " << *description.icmp << "\n";
      llvm::errs() << "Counter Root: " << *description.counterRoot << "\n";
      if (description.init) {
        llvm::errs() << "Init: " << *description.init << "\n";
      } else {
        llvm::errs() << "Init: <unknown>\n";
      }
      llvm::errs() << "}\n";
    }
  }
}

std::optional<LoopCounterICMP>
LoopBoundIDEAnalysis::findCounterFromICMP(llvm::ICmpInst *inst,
                                         llvm::Loop *loop) {
  llvm::Value *LHS = inst->getOperand(0);
  llvm::Value *RHS = inst->getOperand(1);

  llvm::errs() << *inst << "\n";

  auto leftSideRoots  = sliceBackwards(LHS, loop);
  auto rightSideRoots = sliceBackwards(RHS, loop);

  const bool leftHas  = !leftSideRoots.empty();
  const bool rightHas = !rightSideRoots.empty();

  if (leftHas && !rightHas) return LoopCounterICMP{LHS, RHS, leftSideRoots};
  if (!leftHas && rightHas) return LoopCounterICMP{RHS, LHS, rightSideRoots};

  return std::nullopt;
}

bool LoopBoundIDEAnalysis::isIrrelevantToLoop(const llvm::Value *val,
                                             llvm::Loop *loop) {
  if (llvm::isa<llvm::Constant>(val)) {
    return true;
  }

  if (auto *I = llvm::dyn_cast<llvm::Instruction>(val)) {
    return !loop->contains(I);
  }

  if (llvm::isa<llvm::Argument>(val)) {
    return true;
  }
  return false;
}

bool LoopBoundIDEAnalysis::loadIsCarriedIn(const llvm::LoadInst *inst,
                                          llvm::Loop *loop) {
  if (!loop->contains(inst)) return false;

  const llvm::Value *ptr = LoopBound::Util::stripAddr(inst->getPointerOperand());

  for (llvm::BasicBlock *block : loop->blocks()) {
    for (llvm::Instruction &I : *block) {
      if (auto *storeInst = llvm::dyn_cast<llvm::StoreInst>(&I)) {
        if (LoopBound::Util::stripAddr(storeInst->getPointerOperand()) == ptr) {
          return true;
        }
      }
    }
  }

  return false;
}

std::vector<const llvm::Value *>
LoopBoundIDEAnalysis::sliceBackwards(llvm::Value *start, llvm::Loop *loop) {
  std::vector<const llvm::Value *> roots;
  llvm::SmallVector<const llvm::Value *, 32> worklist;
  llvm::DenseSet<const llvm::Value *> visited;
  llvm::DenseSet<const llvm::Value *> rootSet;

  auto push = [&](llvm::Value *V) {
    if (!V) return;
    if (visited.insert(V).second) {
      worklist.push_back(V);
    }
  };

  worklist.push_back(start);

  while (!worklist.empty()) {
    const llvm::Value *curr = worklist.pop_back_val();
    if (!curr) continue;

    if (isIrrelevantToLoop(curr, loop)) {
      continue;
    }

    if (auto *inst = llvm::dyn_cast<llvm::Instruction>(curr)) {
      if (auto *LI = llvm::dyn_cast<llvm::LoadInst>(inst)) {
        if (loadIsCarriedIn(LI, loop)) {
          const llvm::Value *Addr = LoopBound::Util::stripAddr(LI->getPointerOperand());
          if (rootSet.insert(Addr).second) {
            roots.push_back(Addr);
          }
        }
        continue;
      }

      if (loop->contains(inst)) {
        for (llvm::Value *Op : inst->operands()) {
          push(Op);
        }
      }
    }
  }

  return roots;
}

std::vector<LoopParameterDescription> LoopBoundIDEAnalysis::getLoopParameterDescriptions() {
  return LoopDescriptions;
}

} // namespace loopbound
