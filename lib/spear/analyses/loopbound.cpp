#include "analyses/loopbound.h"

#include <phasar/PhasarLLVM/ControlFlow/LLVMBasedCFG.h>

#include <cstdio>
#include <memory>
#include <utility>
#include <llvm/IR/Operator.h>
#include <phasar/PhasarLLVM/DataFlow/IfdsIde/LLVMZeroValue.h>

#include "analyses/LoopBoundEdgeFunction.h"
#include "analyses/util.h"

#include <atomic>

namespace {

template <typename D, typename ContainerT>
class DebugFlow final : public psr::FlowFunction<D, ContainerT> {
  std::shared_ptr<psr::FlowFunction<D, ContainerT>> Inner;
  const char *Name;
  loopbound::LoopBoundIDEAnalysis *A;
  loopbound::LoopBoundIDEAnalysis::n_t Curr;
  loopbound::LoopBoundIDEAnalysis::n_t Succ;

public:
  DebugFlow(std::shared_ptr<psr::FlowFunction<D, ContainerT>> Inner,
            const char *Name,
            loopbound::LoopBoundIDEAnalysis *A,
            loopbound::LoopBoundIDEAnalysis::n_t Curr,
            loopbound::LoopBoundIDEAnalysis::n_t Succ)
      : Inner(std::move(Inner)), Name(Name), A(A), Curr(Curr), Succ(Succ) {}

  ContainerT computeTargets(D Src) override {
    ContainerT Out = Inner->computeTargets(Src);

    if (loopbound::LB_DebugEnabled.load()) {
      llvm::errs() << loopbound::LB_TAG << " FF " << Name << "  ";
      loopbound::dumpInst(Curr);
      llvm::errs() << "  ->  ";
      loopbound::dumpInst(Succ);
      llvm::errs() << "\n" << loopbound::LB_TAG << "   Src=";
      loopbound::dumpFact(A,
                          static_cast<loopbound::LoopBoundIDEAnalysis::d_t>(Src));
      llvm::errs() << "   Targets={";
      bool first = true;
      for (auto T : Out) {
        if (!first) {
          llvm::errs() << ", ";
        }
        first = false;
        loopbound::dumpFact(
            A, static_cast<loopbound::LoopBoundIDEAnalysis::d_t>(T));
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
class GenFromZero final : public psr::FlowFunction<D, ContainerT> {
  D Gen;
  D Zero;

public:
  GenFromZero(D Gen, D Zero) : Gen(Gen), Zero(Zero) {}

  ContainerT computeTargets(D Src) override {
    if (Src == Zero) {
      return ContainerT{Zero, Gen};
    }
    return ContainerT{Src};
  }
};

template <typename D, typename ContainerT>
class KeepOnlyZero final : public psr::FlowFunction<D, ContainerT> {
  D Zero;

public:
  explicit KeepOnlyZero(D Zero) : Zero(Zero) {}

  ContainerT computeTargets(D Src) override {
    if (Src == Zero) {
      return ContainerT{Zero};
    }
    return ContainerT{};
  }
};

template <typename D, typename ContainerT>
class KeepLocalOnCallToRet final : public psr::FlowFunction<D, ContainerT> {
public:
  ContainerT computeTargets(D Src) override { return ContainerT{Src}; }
};

} // namespace

namespace loopbound {

// ======================= LoopBoundIDEAnalysis =======================

LoopBoundIDEAnalysis::LoopBoundIDEAnalysis(const psr::LLVMProjectIRDB *IRDB,
                                           std::vector<std::string> EPs,
                                           std::vector<llvm::Loop *> *loops)
    : base_t(IRDB, EPs,
             std::optional<d_t>(
                 static_cast<d_t>(psr::LLVMZeroValue::getInstance()))),
      IRDBPtr(IRDB),
      EntryPoints(std::move(EPs)) {
  this->loops = loops;
  this->findLoopCounters();
  this->buildCounterRootIndex();
  llvm::errs() << "[LB] CTOR EPs=" << EntryPoints.size() << "\n";
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

    const llvm::Value *CanonRoot = stripAddr(root);

    llvm::errs() << LB_TAG << " Seed @header=";
    dumpInst(headerNode);
    llvm::errs() << "\n" << LB_TAG << "   Zero=";
    dumpFact(this, getZeroValue());
    llvm::errs() << "  -> " << topElement() << "\n";
    llvm::errs() << LB_TAG << "   Root=" << CanonRoot << " (orig=" << root
                 << ") -> [0,0]\n";

    Seeds.addSeed(headerNode, getZeroValue(), topElement());
    Seeds.addSeed(headerNode, static_cast<d_t>(CanonRoot), l_t::empty());
  }

  return Seeds;
}

bool LoopBoundIDEAnalysis::isZeroValue(d_t Fact) const noexcept {
  return base_t::isZeroValue(Fact);
}

LoopBoundIDEAnalysis::FlowFunctionPtrType
LoopBoundIDEAnalysis::getSummaryFlowFunction(n_t, f_t) {
  return nullptr;
}

LoopBoundIDEAnalysis::l_t LoopBoundIDEAnalysis::topElement() {
  return l_t::empty();
}
LoopBoundIDEAnalysis::l_t LoopBoundIDEAnalysis::bottomElement() {
  return l_t::top();
}

LoopBoundIDEAnalysis::l_t LoopBoundIDEAnalysis::join(l_t Lhs, l_t Rhs) {
  l_t Res;

  // Hull accumulation (possible increments)
  // IMPORTANT: in this lattice, topElement() is EMPTY, not TOP.
  if (Lhs.isBottom()) {
    Res = Rhs; // unreachable handling
  } else if (Rhs.isBottom()) {
    Res = Lhs;
  }
  // If either side is IDE-bottom (= DeltaInterval::top()), result stays unknown.
  else if (Lhs.isTop() || Rhs.isTop()) {
    Res = l_t::top();
  }
  // EMPTY behaves like neutral:
  else if (Lhs.isEmpty()) {
    Res = Rhs;
  } else if (Rhs.isEmpty()) {
    Res = Lhs;
  } else {
    Res = Lhs.leastUpperBound(Rhs); // hull
  }

  llvm::errs() << "[LB] join: " << Lhs << " lub " << Rhs << " = " << Res << "\n";
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
    if (LB_DebugEnabled.load()) {
      llvm::errs() << "[LB] CUT edge (no-kill facts): " << *Curr << " -> " << *Succ << "\n";
    }
    auto Inner = std::make_shared<IdentityFlow<d_t, container_t>>();
    return std::make_shared<DebugFlow<d_t, container_t>>(Inner, "Identity", this, Curr, Succ);
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
const LoopDescription *
LoopBoundIDEAnalysis::getLoopDescriptionForInst(const llvm::Instruction *I) const {
  if (!I) {
    return nullptr;
  }
  for (const auto &LD : LoopDescriptions) {
    if (!LD.loop || !LD.counterRoot) {
      continue;
    }
    if (LD.loop->contains(I)) {
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
  V = stripAddr(V);

  const LoopDescription *LD = getLoopDescriptionForInst(AtInst);
  if (!LD || !LD->loop) {
    return false;
  }

  const llvm::Value *Root = stripAddr(LD->counterRoot);

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

// ---------------- Option B: loop-exit summary support ----------------

std::optional<int64_t>
LoopBoundIDEAnalysis::findConstStepForCell(const llvm::Value *Addr, llvm::Loop *L) {
  if (!Addr || !L) {
    return std::nullopt;
  }

  const llvm::Value *Root = stripAddr(Addr);

  for (llvm::BasicBlock *BB : L->blocks()) {
    for (llvm::Instruction &I : *BB) {
      auto *SI = llvm::dyn_cast<llvm::StoreInst>(&I);
      if (!SI) {
        continue;
      }

      if (stripAddr(SI->getPointerOperand()) != Root) {
        continue;
      }

      if (auto inc = extractConstIncFromStore(SI, Root)) {
        return *inc;
      }
    }
  }

  return std::nullopt;
}

static std::optional<int64_t> ceilDivPos(int64_t num, int64_t den) {
  if (den <= 0) {
    return std::nullopt;
  }
  if (num <= 0) {
    return int64_t(0);
  }
  return (num + den - 1) / den;
}

std::optional<int64_t>
LoopBoundIDEAnalysis::computeConstTripCount(const LoopDescription &LD) const {
  if (!LD.loop || !LD.icmp) {
    return std::nullopt;
  }
  if (!LD.init || !LD.step) {
    return std::nullopt;
  }

  int64_t init = *LD.init;
  int64_t step = *LD.step;

  if (step == 0) {
    return std::nullopt;
  }

  auto *C = llvm::dyn_cast<llvm::ConstantInt>(LD.limitExpr);
  if (!C) {
    return std::nullopt;
  }
  int64_t limit = C->getSExtValue();

  using Pred = llvm::CmpInst::Predicate;
  Pred P = LD.icmp->getPredicate();

  // Increasing induction with i < limit / i u< limit
  if ((P == Pred::ICMP_SLT || P == Pred::ICMP_ULT) && step > 0) {
    return ceilDivPos(limit - init, step);
  }

  // Decreasing induction with i > limit / i u> limit (optional)
  if ((P == Pred::ICMP_SGT || P == Pred::ICMP_UGT) && step < 0) {
    return ceilDivPos(init - limit, -step);
  }

  return std::nullopt;
}

bool LoopBoundIDEAnalysis::isExitingToExitEdge(n_t Curr, n_t Succ,
                                              const LoopDescription &LD) const {
  if (!Curr || !Succ || !LD.loop) {
    return false;
  }

  const llvm::BasicBlock *CB = Curr->getParent();
  const llvm::BasicBlock *SB = Succ->getParent();
  if (!CB || !SB) {
    return false;
  }

  // Only consider edges leaving the terminator of the block
  if (CB->getTerminator() != Curr) {
    return false;
  }

  // Curr block must be an exiting block
  if (!LD.loop->isLoopExiting(const_cast<llvm::BasicBlock *>(CB))) {
    return false;
  }

  // Succ must be outside the loop
  if (LD.loop->contains(SB)) {
    return false;
  }

  // Succ must be the first instruction of successor block (node model)
  if (&*SB->begin() != Succ) {
    return false;
  }

  // Sanity: SB is actually a successor of CB
  bool ok = false;
  for (const llvm::BasicBlock *S : llvm::successors(CB)) {
    if (S == SB) {
      ok = true;
      break;
    }
  }
  return ok;
}

// === Instrument getNormalEdgeFunction() heavily ===
LoopBoundIDEAnalysis::EdgeFunctionType
LoopBoundIDEAnalysis::getNormalEdgeFunction(n_t curr, d_t currNode, n_t Succ,
                                            d_t succNode) {

  // === FIX 2: cut-edge must be EF-neutral (no accumulation) ===
  // You already cut the *flow* (Fix 1). This additionally ensures the
  // *edge function* cannot create a growing composed EF along the loop.
  // === Backedge: reset per-iteration collected increment interval ===
  if (isLatchToHeaderEdge(curr, Succ)) {
    return EF(std::in_place_type<DeltaIntervalIdentity>);

    // Leave zero and fact changes alone.
    if (isZeroValue(currNode) || isZeroValue(succNode) || currNode != succNode) {
      return EF(std::in_place_type<DeltaIntervalIdentity>);
    }

    // Only reset the loop counter-root fact.
    if (isCounterRootFactAtInst(currNode, curr)) {
      // Encode EMPTY as (lower > upper).
      // This requires DeltaIntervalAssign::computeTarget to treat lower>upper as empty().
      auto E = EF(std::in_place_type<DeltaIntervalAssign>, 1, 0);

      if (LB_DebugEnabled.load()) {
        llvm::errs() << LB_TAG << " EF normal (CUT latch->header RESET) @";
        dumpInst(curr);
        llvm::errs() << "  ->  ";
        dumpInst(Succ);
        llvm::errs() << "   ";
        dumpEF(E);
        llvm::errs() << "\n";
      }

      return E;
    }

    return EF(std::in_place_type<DeltaIntervalIdentity>);
  }

  if (LB_DebugEnabled.load()) {
    llvm::errs() << LB_TAG << " EF normal @";
    dumpInst(curr);
    llvm::errs() << "\n" << LB_TAG << "   currFact=";
    dumpFact(this, currNode);
    llvm::errs() << "\n" << LB_TAG << "   succFact=";
    dumpFact(this, succNode);
    llvm::errs() << "\n";
  }

  if (isZeroValue(currNode) || isZeroValue(succNode)) {
    auto E = EF(std::in_place_type<DeltaIntervalIdentity>);
    if (LB_DebugEnabled.load()) {
      llvm::errs() << LB_TAG << "   reason=zero-involved  ";
      dumpEF(E);
      llvm::errs() << "\n";
    }
    return E;
  }

  if (currNode != succNode) {
    auto E = EF(std::in_place_type<DeltaIntervalIdentity>);
    if (LB_DebugEnabled.load()) {
      llvm::errs() << LB_TAG << "   reason=fact-changed(curr!=succ)  ";
      dumpEF(E);
      llvm::errs() << "\n";
    }
    return E;
  }

  const bool IsRootHere = isCounterRootFactAtInst(currNode, curr);
  if (LB_DebugEnabled.load()) {
    llvm::errs() << LB_TAG << "   isCounterRootFactAtInst="
                 << (IsRootHere ? "true" : "false") << "\n";
  }

  if (!IsRootHere) {
    auto E = EF(std::in_place_type<DeltaIntervalIdentity>);
    if (LB_DebugEnabled.load()) {
      llvm::errs() << LB_TAG << "   reason=not-root-fact  ";
      dumpEF(E);
      llvm::errs() << "\n";
    }
    return E;
  }

  // === Option B: loop-exit summary injection on exiting edge ===
  if (const auto *LD = getLoopDescriptionForInst(curr)) {
    if (isExitingToExitEdge(curr, Succ, *LD)) {

      if (auto k = computeConstTripCount(*LD)) {
        if (LD->step) {
          int64_t step = *LD->step;
          int64_t finalVal = (*k) * step;

          auto E = EF(std::in_place_type<DeltaIntervalAssign>, finalVal, finalVal);

          if (LB_DebugEnabled.load()) {
            llvm::errs() << LB_TAG << "   LOOP-SUMMARY exit edge: k=" << *k
                         << " step=" << step << " final=" << finalVal << "  ";
            dumpEF(E);
            llvm::errs() << "\n";
          }

          return E;
        }
      }

      auto E = EF(std::in_place_type<DeltaIntervalTop>);
      if (LB_DebugEnabled.load()) {
        llvm::errs() << LB_TAG << "   LOOP-SUMMARY exit edge: unknown -> ";
        dumpEF(E);
        llvm::errs() << "\n";
      }
      return E;
    }
  }

  // === per-store increment EF ===
  if (auto *storeInst = llvm::dyn_cast<llvm::StoreInst>(curr)) {
    const llvm::Value *root =
        stripAddr(static_cast<const llvm::Value *>(currNode));

    if (auto increment = extractConstIncFromStore(storeInst, root)) {
      auto E = EF(std::in_place_type<DeltaIntervalCollect>,
                  static_cast<int64_t>(*increment),
                  static_cast<int64_t>(*increment));

      if (LB_DebugEnabled.load()) {
        llvm::errs() << "[LB] INC matched at store: " << *storeInst
                     << "  inc=" << *increment << "\n";
        llvm::errs() << LB_TAG << "   produced ";
        dumpEF(E);
        llvm::errs() << "\n";
      }
      return E;
    }
  }

  auto E = EF(std::in_place_type<DeltaIntervalIdentity>);
  if (LB_DebugEnabled.load()) {
    llvm::errs() << LB_TAG << "   reason=no-inc  ";
    dumpEF(E);
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

  const llvm::Value *destination = stripAddr(storeInst->getPointerOperand());
  const llvm::Value *root        = stripAddr(counterRoot);

  if (LB_DebugEnabled.load()) {
    llvm::errs() << LB_TAG << " extractConstIncFromStore\n";
    llvm::errs() << LB_TAG << "   store=" << *storeInst << "\n";
    llvm::errs() << LB_TAG << "   dst=" << destination << "  root=" << root << "\n";
  }

  if (destination != root) {
    if (LB_DebugEnabled.load()) {
      llvm::errs() << LB_TAG << "   FAIL: destination!=root\n";
    }
    return std::nullopt;
  }

  const llvm::Value *value = storeInst->getValueOperand();
  auto *binaryOperator = llvm::dyn_cast<llvm::BinaryOperator>(value);
  if (!binaryOperator) {
    if (LB_DebugEnabled.load()) {
      llvm::errs() << LB_TAG
                   << "   FAIL: value not BinaryOperator, value=" << *value
                   << "\n";
    }
    return std::nullopt;
  }

  llvm::Value *firstOperand  = binaryOperator->getOperand(0);
  llvm::Value *secondOperand = binaryOperator->getOperand(1);

  if (LB_DebugEnabled.load()) {
    llvm::errs() << LB_TAG << "   binop=" << *binaryOperator << "\n";
  }

  switch (binaryOperator->getOpcode()) {
    case llvm::Instruction::Add: {
      if (isLoadOfCounterRoot(firstOperand, root)) {
        if (auto *constantIncrement =
                llvm::dyn_cast<llvm::ConstantInt>(secondOperand)) {
          if (LB_DebugEnabled.load()) {
            llvm::errs() << LB_TAG << "   OK: load(root)+C  C="
                         << constantIncrement->getSExtValue() << "\n";
          }
          return constantIncrement->getSExtValue();
        }
      }

      if (isLoadOfCounterRoot(secondOperand, root)) {
        if (auto *constantIncrement =
                llvm::dyn_cast<llvm::ConstantInt>(firstOperand)) {
          if (LB_DebugEnabled.load()) {
            llvm::errs() << LB_TAG << "   OK: C+load(root)  C="
                         << constantIncrement->getSExtValue() << "\n";
          }
          return constantIncrement->getSExtValue();
        }
      }

      if (LB_DebugEnabled.load()) {
        llvm::errs() << LB_TAG << "   FAIL: add but not (load(root)+C)\n";
      }
      return std::nullopt;
    }

    case llvm::Instruction::Sub: {
      if (isLoadOfCounterRoot(firstOperand, root)) {
        if (auto *constantIncrement =
                llvm::dyn_cast<llvm::ConstantInt>(secondOperand)) {
          if (LB_DebugEnabled.load()) {
            llvm::errs() << LB_TAG << "   OK: load(root)-C  C="
                         << constantIncrement->getSExtValue() << "\n";
          }
          return -constantIncrement->getSExtValue();
        }
      }

      if (LB_DebugEnabled.load()) {
        llvm::errs() << LB_TAG << "   FAIL: sub but not (load(root)-C)\n";
      }
      return std::nullopt;
    }

    default: {
      if (LB_DebugEnabled.load()) {
        llvm::errs() << LB_TAG << "   FAIL: opcode not add/sub\n";
      }
      return std::nullopt;
    }
  }
}


bool LoopBoundIDEAnalysis::isLoadOfCounterRoot(llvm::Value *value,
                                              const llvm::Value *root) {
  if (auto *loopInstruction = llvm::dyn_cast<llvm::LoadInst>(value)) {
    const llvm::Value *ptr = stripAddr(loopInstruction->getPointerOperand());
    return ptr == root;
  }
  return false;
}

std::optional<int64_t>
LoopBoundIDEAnalysis::findConstInitForCell(const llvm::Value *Addr,
                                          llvm::Loop *L) {
  auto strippedAddr = stripAddr(Addr);
  llvm::BasicBlock *PreH = L->getLoopPreheader();
  if (!PreH) return std::nullopt;

  for (llvm::Instruction &I : *PreH) {
    auto *SI = llvm::dyn_cast<llvm::StoreInst>(&I);
    if (!SI) continue;
    if (stripAddr(SI->getPointerOperand()) != strippedAddr) continue;

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

      // Option B: infer constant step within the loop (if present)
      auto step = findConstStepForCell(info->Roots[0], loop);

      llvm::Function *LoopF = loop->getHeader()->getParent();

      auto *RootI = llvm::dyn_cast<llvm::Instruction>(info->Roots[0]);
      if (RootI && RootI->getFunction() != LoopF) continue;

      auto *RootA = llvm::dyn_cast<llvm::AllocaInst>(info->Roots[0]);
      if (RootA && RootA->getFunction() != LoopF) continue;

      LoopDescription description = {
          loop,
          icmp,
          info->Roots[0],
          info->CounterSide,
          info->InvariantSide,
          init,
          step // <-- NEW
      };

      LoopDescriptions.push_back(description);

      llvm::errs() << "Generated Loop Description {\n";
      llvm::errs() << "Loop: " << description.loop->getName() << "\n";
      llvm::errs() << "ICMP: " << *description.icmp << "\n";
      llvm::errs() << "Counter Root: " << *description.counterRoot << "\n";
      llvm::errs() << "Counter Exp: " << *description.counterExpr << "\n";
      llvm::errs() << "Invariant: " << *description.limitExpr << "\n";
      if (description.init) {
        llvm::errs() << "Init: " << *description.init << "\n";
      } else {
        llvm::errs() << "Init: <unknown>\n";
      }
      if (description.step) {
        llvm::errs() << "Step: " << *description.step << "\n";
      } else {
        llvm::errs() << "Step: <unknown>\n";
      }
      llvm::errs() << "}\n";
    }
  }
}

const llvm::Value *LoopBoundIDEAnalysis::stripAddr(const llvm::Value *Ptr) {
  Ptr = Ptr->stripPointerCasts();

  while (true) {
    if (auto *GEP = llvm::dyn_cast<llvm::GEPOperator>(Ptr)) {
      Ptr = GEP->getPointerOperand()->stripPointerCasts();
      continue;
    }

    if (auto *OP = llvm::dyn_cast<llvm::Operator>(Ptr)) {
      switch (OP->getOpcode()) {
        case llvm::Instruction::BitCast:
        case llvm::Instruction::AddrSpaceCast:
          Ptr = OP->getOperand(0)->stripPointerCasts();
          continue;
        default:
          break;
      }
    }

    break;
  }

  return Ptr;
}

std::optional<CounterFromIcmp>
LoopBoundIDEAnalysis::findCounterFromICMP(llvm::ICmpInst *inst,
                                         llvm::Loop *loop) {
  llvm::Value *LHS = inst->getOperand(0);
  llvm::Value *RHS = inst->getOperand(1);

  llvm::errs() << *inst << "\n";

  auto leftSideRoots  = sliceBackwards(LHS, loop);
  auto rightSideRoots = sliceBackwards(RHS, loop);

  const bool leftHas  = !leftSideRoots.empty();
  const bool rightHas = !rightSideRoots.empty();

  if (leftHas && !rightHas) return CounterFromIcmp{LHS, RHS, leftSideRoots};
  if (!leftHas && rightHas) return CounterFromIcmp{RHS, LHS, rightSideRoots};

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

bool LoopBoundIDEAnalysis::phiHasIncomingValueFromLoop(const llvm::PHINode *phi,
                                                      llvm::Loop *loop) {
  if (!loop->contains(phi) || phi->getParent() != loop->getHeader()) {
    return false;
  }

  for (int i = 0; i < (int)phi->getNumIncomingValues(); i++) {
    if (loop->contains(phi->getIncomingBlock(i))) {
      return true;
    }
  }

  return false;
}

bool LoopBoundIDEAnalysis::ptrDependsOnLoopCariedPhi(const llvm::Value *ptr,
                                                    llvm::Loop *loop) {
  llvm::SmallVector<const llvm::Value *, 32> worklist;
  llvm::DenseSet<const llvm::Value *> visited;

  auto push = [&](llvm::Value *V) {
    if (!V) return;
    auto strippedVal = stripAddr(V);
    if (visited.insert(strippedVal).second) {
      worklist.push_back(strippedVal);
    }
  };

  worklist.push_back(ptr);
  while (!worklist.empty()) {
    const llvm::Value *curr = worklist.pop_back_val();

    if (auto *phi = llvm::dyn_cast<llvm::PHINode>(curr)) {
      if (phiHasIncomingValueFromLoop(phi, loop)) {
        return true;
      }
      for (llvm::Value *incomingVal : phi->incoming_values()) {
        push(incomingVal);
      }
    }

    if (auto *inst = llvm::dyn_cast<llvm::Instruction>(curr)) {
      if (loop->contains(inst)) {
        for (llvm::Value *op : inst->operands()) {
          push(op);
        }
      }
    }
  }

  return false;
}

bool LoopBoundIDEAnalysis::loadIsCarriedIn(const llvm::LoadInst *inst,
                                          llvm::Loop *loop) {
  if (!loop->contains(inst)) return false;

  const llvm::Value *ptr = stripAddr(inst->getPointerOperand());

  for (llvm::BasicBlock *block : loop->blocks()) {
    for (llvm::Instruction &I : *block) {
      if (auto *storeInst = llvm::dyn_cast<llvm::StoreInst>(&I)) {
        if (stripAddr(storeInst->getPointerOperand()) == ptr) {
          return true;
        }
      }
    }
  }

  if (ptrDependsOnLoopCariedPhi(ptr, loop)) {
    return true;
  }

  return false;
}

void LoopBoundIDEAnalysis::buildCounterRootIndex() {
  CounterRoots.clear();
  CounterRootsPerLoop.clear();

  for (const LoopDescription &LD : LoopDescriptions) {
    if (!LD.loop || !LD.counterRoot) continue;

    const llvm::Value *Root = stripAddr(LD.counterRoot);

    CounterRoots.insert(Root);
    CounterRootsPerLoop[LD.loop].insert(Root);
  }
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

    if (auto *phinst = llvm::dyn_cast<llvm::PHINode>(curr)) {
      if (phiHasIncomingValueFromLoop(phinst, loop)) {
        if (rootSet.insert(phinst).second) {
          roots.push_back(phinst);
        }
        continue;
      }

      for (llvm::Value *incomingVal : phinst->incoming_values()) {
        push(incomingVal);
      }
      continue;
    }

    if (auto *inst = llvm::dyn_cast<llvm::Instruction>(curr)) {
      if (auto *LI = llvm::dyn_cast<llvm::LoadInst>(inst)) {
        if (loadIsCarriedIn(LI, loop)) {
          const llvm::Value *Addr = stripAddr(LI->getPointerOperand());
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

std::vector<LoopDescription> LoopBoundIDEAnalysis::getLoopDescriptions() {
  return LoopDescriptions;
}

} // namespace loopbound
