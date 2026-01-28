#include "analyses/loopbound.h"

#include <phasar/PhasarLLVM/ControlFlow/LLVMBasedCFG.h>

#include <cstdio>
#include <memory>
#include <utility>
#include <llvm/IR/Operator.h>
#include <phasar/PhasarLLVM/DataFlow/IfdsIde/LLVMZeroValue.h>

#include "analyses/LoopBoundEdgeFunction.h"

namespace loopbound {

// ======================= Flow helpers =======================

namespace {

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

// Keep only the ZeroValue across interprocedural edges (kill all real facts)
template <typename D, typename ContainerT>
class KeepOnlyZero final : public psr::FlowFunction<D, ContainerT> {
  D Zero;

public:
  explicit KeepOnlyZero(D Zero) : Zero(Zero) {}

  ContainerT computeTargets(D Src) override {
    if (Src == Zero) {
      return ContainerT{Zero};
    }
    return ContainerT{}; // kill non-zero facts
  }
};

// On call-to-ret, keep caller-local facts (do NOT enter callee)
template <typename D, typename ContainerT>
class KeepLocalOnCallToRet final : public psr::FlowFunction<D, ContainerT> {
public:
  ContainerT computeTargets(D Src) override { return ContainerT{Src}; }
};

} // namespace

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

psr::InitialSeeds<LoopBoundIDEAnalysis::n_t, LoopBoundIDEAnalysis::d_t,
                  LoopBoundIDEAnalysis::l_t>
LoopBoundIDEAnalysis::initialSeeds() {
  psr::InitialSeeds<n_t, d_t, l_t> Seeds;

  for (auto &desc : LoopDescriptions) {
    llvm::Loop *loop = desc.loop;
    const llvm::Value *root = desc.counterRoot;

    if (!loop || !root) continue;

    llvm::BasicBlock *header = loop->getHeader();
    if (!header || header->empty()) continue;

    n_t headerNode = &*header->begin();

    Seeds.addSeed(headerNode, getZeroValue(), bottomElement());
    Seeds.addSeed(headerNode, static_cast<d_t>(stripAddr(root)), l_t::interval(0, 0));
  }

  return Seeds;
}

bool LoopBoundIDEAnalysis::isZeroValue(d_t Fact) const noexcept {
  return base_t::isZeroValue(Fact);
}

LoopBoundIDEAnalysis::l_t LoopBoundIDEAnalysis::topElement() { return l_t::top(); }
LoopBoundIDEAnalysis::l_t LoopBoundIDEAnalysis::bottomElement() { return l_t::bottom(); }

LoopBoundIDEAnalysis::l_t LoopBoundIDEAnalysis::join(l_t Lhs, l_t Rhs) {
  auto Res = Lhs.join(Rhs);
  if (Lhs != Rhs) {

  }

  llvm::errs() << "[LB] join: " << Lhs << " ⊓ " << Rhs << " = " << Res << "\n";

  return Res;
}

psr::EdgeFunction<LoopBoundIDEAnalysis::l_t>
LoopBoundIDEAnalysis::allTopFunction() {
  return psr::AllTop<l_t>{};
}

// ---------------- Flow functions ----------------

LoopBoundIDEAnalysis::FlowFunctionPtrType
LoopBoundIDEAnalysis::getNormalFlowFunction(n_t Curr, n_t /*Succ*/) {
  if (const auto *icmpinst = llvm::dyn_cast<llvm::ICmpInst>(Curr)) {
    // llvm::errs() << "[LB] ICmpInst: " << icmpinst->getOperand(0) << " : "
    //              << icmpinst->getOperand(1) << "\n";
  }

  return std::make_shared<IdentityFlow<d_t, container_t>>();
}

LoopBoundIDEAnalysis::FlowFunctionPtrType
LoopBoundIDEAnalysis::getCallFlowFunction(n_t, f_t) {
  // Do not propagate stack-cell facts into the callee.
  return std::make_shared<KeepOnlyZero<d_t, container_t>>(getZeroValue());
}

LoopBoundIDEAnalysis::FlowFunctionPtrType
LoopBoundIDEAnalysis::getRetFlowFunction(n_t, f_t, n_t, n_t) {
  // Do not bring callee facts back either.
  return std::make_shared<KeepOnlyZero<d_t, container_t>>(getZeroValue());
}

LoopBoundIDEAnalysis::FlowFunctionPtrType
LoopBoundIDEAnalysis::getCallToRetFlowFunction(n_t, n_t, llvm::ArrayRef<f_t>) {
  // Skip the call but keep facts in the caller.
  return std::make_shared<KeepLocalOnCallToRet<d_t, container_t>>();
}

LoopBoundIDEAnalysis::FlowFunctionPtrType
LoopBoundIDEAnalysis::getSummaryFlowFunction(n_t, f_t) {
  return nullptr;
}

// ---------------- Edge functions ----------------

// Helper: determine which LoopDescription applies at a program point (curr instruction).
// We keep it cheap: pick the innermost loop whose header dominates the instruction block
// by using Loop::contains.
const LoopDescription *
LoopBoundIDEAnalysis::getLoopDescriptionForInst(const llvm::Instruction *I) const {
  if (!I) return nullptr;
  for (const auto &LD : LoopDescriptions) {
    if (!LD.loop || !LD.counterRoot) continue;
    if (LD.loop->contains(I)) {
      return &LD;
    }
  }
  return nullptr;
}

bool LoopBoundIDEAnalysis::isCounterRootFactAtInst(d_t Fact, n_t AtInst) const {
  if (!Fact || isZeroValue(Fact) || !AtInst) return false;

  const auto *V = static_cast<const llvm::Value *>(Fact);
  V = stripAddr(V);

  const LoopDescription *LD = getLoopDescriptionForInst(AtInst);
  if (!LD || !LD->loop) return false;

  const llvm::Value *Root = stripAddr(LD->counterRoot);

  // Also guard by function (stack allocas are function-local)
  const llvm::Function *F1 = AtInst->getFunction();
  const llvm::Function *F2 = nullptr;
  if (auto *RI = llvm::dyn_cast<llvm::Instruction>(Root)) {
    F2 = RI->getFunction();
  } else if (auto *RA = llvm::dyn_cast<llvm::AllocaInst>(Root)) {
    F2 = RA->getFunction();
  }
  if (F2 && F1 != F2) return false;

  return V == Root;
}

LoopBoundIDEAnalysis::EdgeFunctionType
LoopBoundIDEAnalysis::getNormalEdgeFunction(n_t curr, d_t currNode, n_t /*Succ*/,
                                            d_t succNode) {
  if (isZeroValue(currNode) || isZeroValue(succNode)) {
    EF(std::in_place_type<DeltaIntervalIdentity>);
  }

  if (currNode != succNode) {
    EF(std::in_place_type<DeltaIntervalIdentity>);
  }

  // LOOP-/FUNCTION-LOCAL counter-root check (replaces the global check)
  if (!isCounterRootFactAtInst(currNode, curr)) {
    EF(std::in_place_type<DeltaIntervalIdentity>);
  }

  //llvm::errs() << "[fact] " << *curr << "\n";

  if (auto *storeInst = llvm::dyn_cast<llvm::StoreInst>(curr)) {
    const llvm::Value *root = stripAddr(static_cast<const llvm::Value *>(currNode));

    if (auto increment = extractConstIncFromStore(storeInst, root)) {
      llvm::errs() << "[LB] INC matched at store: " << *storeInst
                   << "  step=" << *increment << "\n";
      llvm::errs() << "[" << *increment << ", " << *increment << "]\n";
      //return DeltaIntervalNormal(*increment, *increment);
      return EF(std::in_place_type<DeltaIntervalNormal>, *increment, *increment);
    }

    /*const llvm::Value *destination = stripAddr(storeInst->getPointerOperand());
    if (destination == root) {
    return EF(std::in_place_type<DeltaIntervaTop>);
    }*/
  }

  return EF(std::in_place_type<DeltaIntervalIdentity>);
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


std::optional<int64_t>
LoopBoundIDEAnalysis::extractConstIncFromStore(const llvm::StoreInst *storeInst,
                                               const llvm::Value *counterRoot) {
  if (!storeInst || !counterRoot) return std::nullopt;

  const llvm::Value *destination = stripAddr(storeInst->getPointerOperand());
  const llvm::Value *root        = stripAddr(counterRoot);

  if (destination != root) {
    /*llvm::errs() << "[LB] store dst: " << *destination << " @" << destination << "\n";
    llvm::errs() << "[LB] root     : " << *root        << " @" << root        << "\n";
    llvm::errs() << "[LB] storeinst FUNC: " << storeInst->getFunction()->getName() << "\n";
    if (auto *RI = llvm::dyn_cast<llvm::Instruction>(counterRoot)) {
      llvm::errs() << "[LB] counteroot FUNC: " << RI->getFunction()->getName() << "\n";
    } else if (auto *RA = llvm::dyn_cast<llvm::AllocaInst>(counterRoot)) {
      llvm::errs() << "[LB] counteroot FUNC: " << RA->getFunction()->getName() << "\n";
    }*/
    return std::nullopt;
  }

  auto *value = const_cast<llvm::Value *>(storeInst->getValueOperand());

  auto binaryOperator = llvm::dyn_cast<llvm::BinaryOperator>(value);
  if (!binaryOperator) {
    return std::nullopt;
  }

  llvm::Value *firstOperand = binaryOperator->getOperand(0);
  llvm::Value *secondOperand = binaryOperator->getOperand(1);

  switch (binaryOperator->getOpcode()) {
    case llvm::Instruction::Add: {
      if (isLoadOfCounterRoot(firstOperand, root)) {
        if (auto constantIncrement = llvm::dyn_cast<llvm::ConstantInt>(secondOperand)) {
          return constantIncrement->getSExtValue();
        }
      }
      if (isLoadOfCounterRoot(secondOperand, root)) {
        if (auto constantIncrement = llvm::dyn_cast<llvm::ConstantInt>(firstOperand)) {
          return constantIncrement->getSExtValue();
        }
      }
      return std::nullopt;
    }

    case llvm::Instruction::Sub: {
      if (isLoadOfCounterRoot(firstOperand, root)) {
        if (auto constantIncrement = llvm::dyn_cast<llvm::ConstantInt>(secondOperand)) {
          return -constantIncrement->getSExtValue();
        }
      }
      return std::nullopt;
    }

    default:
      return std::nullopt;
  }
}

bool LoopBoundIDEAnalysis::isLoadOfCounterRoot(llvm::Value *value, const llvm::Value *root) {
  if (auto *loopInstruction = llvm::dyn_cast<llvm::LoadInst>(value)) {
    const llvm::Value *ptr = stripAddr(loopInstruction->getPointerOperand());
    return ptr == root;
  }
  return false;
}

std::optional<int64_t>
LoopBoundIDEAnalysis::findConstInitForCell(const llvm::Value *Addr, llvm::Loop *L) {
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
        init
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
LoopBoundIDEAnalysis::findCounterFromICMP(llvm::ICmpInst *inst, llvm::Loop *loop) {
  llvm::Value *LHS = inst->getOperand(0);
  llvm::Value *RHS = inst->getOperand(1);

  llvm::errs() << *inst << "\n";

  auto leftSideRoots = sliceBackwards(LHS, loop);
  auto rightSideRoots = sliceBackwards(RHS, loop);

  const bool leftHas  = !leftSideRoots.empty();
  const bool rightHas = !rightSideRoots.empty();

  if (leftHas && !rightHas) return CounterFromIcmp{LHS, RHS, leftSideRoots};
  if (!leftHas && rightHas) return CounterFromIcmp{RHS, LHS, rightSideRoots};

  return std::nullopt;
}

bool LoopBoundIDEAnalysis::isIrrelevantToLoop(const llvm::Value *val, llvm::Loop *loop) {
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

bool LoopBoundIDEAnalysis::phiHasIncomingValueFromLoop(const llvm::PHINode *phi, llvm::Loop *loop) {
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

bool LoopBoundIDEAnalysis::ptrDependsOnLoopCariedPhi(const llvm::Value *ptr, llvm::Loop *loop) {
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

bool LoopBoundIDEAnalysis::loadIsCarriedIn(const llvm::LoadInst *inst, llvm::Loop *loop) {
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

} // namespace loopbound