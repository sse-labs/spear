#include "analyses/loopbound.h"

#include <phasar/PhasarLLVM/ControlFlow/LLVMBasedCFG.h>

#include <cstdio>
#include <memory>
#include <utility>
#include <phasar/PhasarLLVM/DataFlow/IfdsIde/LLVMZeroValue.h>

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

} // namespace

// ======================= LoopBoundIDEAnalysis =======================

LoopBoundIDEAnalysis::LoopBoundIDEAnalysis(const psr::LLVMProjectIRDB *IRDB,
                                           std::vector<std::string> EPs, std::vector<llvm::Loop*> *loops)
    : base_t(IRDB, EPs,
             std::optional<d_t>(
                 static_cast<d_t>(psr::LLVMZeroValue::getInstance()))),
      IRDBPtr(IRDB),
      EntryPoints(std::move(EPs)) {

  this->loops = loops;
  this->findLoopCounters();
  llvm::errs() << "[LB] CTOR EPs=" << EntryPoints.size() << "\n";
}

psr::InitialSeeds<LoopBoundIDEAnalysis::n_t, LoopBoundIDEAnalysis::d_t,
                  LoopBoundIDEAnalysis::l_t>
LoopBoundIDEAnalysis::initialSeeds() {
  psr::InitialSeeds<n_t, d_t, l_t> Seeds;

  psr::LLVMBasedCFG CFG;
  addSeedsForStartingPoints(EntryPoints, IRDBPtr, CFG, Seeds, getZeroValue(),
                            bottomElement());

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
    llvm::errs() << "[LB] join: " << Lhs << " ⊓ " << Rhs << " = " << Res << "\n";
  }
  return Res;
}

psr::EdgeFunction<LoopBoundIDEAnalysis::l_t>
LoopBoundIDEAnalysis::allTopFunction() {
  // CRITICAL: must be the real EdgeFunction TOP. Don't use your custom edgeTop().
  return psr::AllTop<l_t>{};
}

// ---------------- Flow functions ----------------

LoopBoundIDEAnalysis::FlowFunctionPtrType
LoopBoundIDEAnalysis::getNormalFlowFunction(n_t Curr, n_t Succ) {
  llvm::errs() << "[LB] getNormalFlowFunction: " << *Curr << " -> " << *Succ << "\n";

  // Generate a fact for integer allocas from zero
  /*if (const auto *Alloca = llvm::dyn_cast<llvm::AllocaInst>(Curr)) {
    auto *AT = Alloca->getAllocatedType();
    if (AT && AT->isIntegerTy()) {
      return std::make_shared<GenFromZero<d_t, container_t>>(static_cast<d_t>(Alloca), getZeroValue());
    }
  }*/


  if (const auto *icmpinst = llvm::dyn_cast<llvm::ICmpInst>(Curr)) {
    llvm::errs() << "[LB] ICmpInst: " << icmpinst->getOperand(0) << " : " << icmpinst->getOperand(1) << "\n";
  }


  return std::make_shared<IdentityFlow<d_t, container_t>>();
}

LoopBoundIDEAnalysis::FlowFunctionPtrType
LoopBoundIDEAnalysis::getCallFlowFunction(n_t, f_t) {
  return std::make_shared<IdentityFlow<d_t, container_t>>();
}

LoopBoundIDEAnalysis::FlowFunctionPtrType
LoopBoundIDEAnalysis::getRetFlowFunction(n_t, f_t, n_t, n_t) {
  return std::make_shared<IdentityFlow<d_t, container_t>>();
}

LoopBoundIDEAnalysis::FlowFunctionPtrType
LoopBoundIDEAnalysis::getCallToRetFlowFunction(n_t, n_t, llvm::ArrayRef<f_t>) {
  return std::make_shared<IdentityFlow<d_t, container_t>>();
}

LoopBoundIDEAnalysis::FlowFunctionPtrType
LoopBoundIDEAnalysis::getSummaryFlowFunction(n_t, f_t) {
  return nullptr;
}

// ---------------- Edge functions ----------------

LoopBoundIDEAnalysis::EdgeFunctionType
LoopBoundIDEAnalysis::getNormalEdgeFunction(n_t Curr, d_t CurrNode, n_t /*Succ*/,
                                            d_t SuccNode) {

  if (isZeroValue(CurrNode) && !isZeroValue(SuccNode)) {
    if (llvm::isa<llvm::AllocaInst>(Curr)) {
      return psr::AllBottom<l_t>{};
    }
  }

  return psr::EdgeIdentity<l_t>{};
}

LoopBoundIDEAnalysis::EdgeFunctionType
LoopBoundIDEAnalysis::getCallEdgeFunction(n_t, d_t, f_t, d_t) {
  return psr::EdgeIdentity<l_t>{};
}

LoopBoundIDEAnalysis::EdgeFunctionType
LoopBoundIDEAnalysis::getReturnEdgeFunction(n_t, f_t, n_t, d_t, n_t, d_t) {
  return psr::EdgeIdentity<l_t>{};
}

LoopBoundIDEAnalysis::EdgeFunctionType
LoopBoundIDEAnalysis::getCallToRetEdgeFunction(n_t, d_t, n_t, d_t,
                                               llvm::ArrayRef<f_t>) {
  return psr::EdgeIdentity<l_t>{};
}

std::optional<int64_t> LoopBoundIDEAnalysis::findConstInitForCell(llvm::Value *Addr, llvm::Loop *L) {
  Addr = stripAddr(Addr);
  llvm::BasicBlock *PreH = L->getLoopPreheader();
  if (!PreH) return std::nullopt;

  for (llvm::Instruction &I : *PreH) {
    auto *SI = llvm::dyn_cast<llvm::StoreInst>(&I);
    if (!SI) continue;
    if (stripAddr(SI->getPointerOperand()) != Addr) continue;

    if (auto *CI = llvm::dyn_cast<llvm::ConstantInt>(SI->getValueOperand()))
      return CI->getSExtValue();
  }
  return std::nullopt;
}

void LoopBoundIDEAnalysis::findLoopCounters() {
  // Iterate over the loops
  for (auto loop : *this->loops) {

    llvm::SmallVector<llvm::BasicBlock *, 8> ExitingBlocks;
    loop->getExitingBlocks(ExitingBlocks);

    /**
     * We start our loop counter detection from the exiting blocks of the loop
     * The exiting block of the loop performs a check on whether to traverse the backedge
     * or the edge to a block outside of the loop.
     */
    for (llvm::BasicBlock *EB : ExitingBlocks) {
      auto *br = llvm::dyn_cast<llvm::BranchInst>(EB->getTerminator());
      if (!br || !br->isConditional())
        continue;

      llvm::Value *cond = br->getCondition();
      auto *icmp = llvm::dyn_cast<llvm::ICmpInst>(cond);
      if (!icmp)
        continue;

      auto info = findCounterFromICMP(icmp, loop);

      if (!info || info->Roots.empty()) {
        continue;
      }

      auto init = findConstInitForCell(info->Roots[0], loop);

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
      llvm::errs() << "Init: " << *description.init << "\n";
      llvm::errs() << "}\n";

    }
  }
}

llvm::Value* LoopBoundIDEAnalysis::stripAddr(llvm::Value *Ptr) {

  while (true) {
    if (auto *GEP = llvm::dyn_cast<llvm::GetElementPtrInst>(Ptr)) {
      Ptr = GEP->getPointerOperand();
      continue;
    }
    if (auto *BC = llvm::dyn_cast<llvm::BitCastInst>(Ptr)) {
      Ptr = BC->getOperand(0);
      continue;
    }
    if (auto *ASC = llvm::dyn_cast<llvm::AddrSpaceCastInst>(Ptr)) {
      Ptr = ASC->getOperand(0);
      continue;
    }
    break;
  }
  return Ptr;
}


std::optional<CounterFromIcmp> LoopBoundIDEAnalysis::findCounterFromICMP(llvm::ICmpInst *inst, llvm::Loop *loop) {
  llvm::Value *LHS = inst->getOperand(0);
  llvm::Value *RHS = inst->getOperand(1);

  llvm::errs() << *inst << "\n";

  auto leftSideRoots = sliceBackwards(LHS, loop);
  auto rightSideRoots = sliceBackwards(RHS, loop);

  const bool leftEmpty = !leftSideRoots.empty();
  const bool rightEmpty = !rightSideRoots.empty();

  if (leftEmpty && !rightEmpty) {
    return CounterFromIcmp{LHS, RHS, leftSideRoots};
  }

  if (!leftEmpty && rightEmpty) {
    return CounterFromIcmp{RHS, LHS, rightSideRoots};
  }

  return std::nullopt;
}


bool LoopBoundIDEAnalysis::isIrrelevantToLoop(llvm::Value *val, llvm::Loop *loop) {
  if (llvm::isa<llvm::Constant>(val)) {
    return true;
  }

  if (auto *I = llvm::dyn_cast<llvm::Instruction>(val))
    return !loop->contains(I);

  if (llvm::isa<llvm::Argument>(val)) {
    return true;
  }
  return false;
}

bool LoopBoundIDEAnalysis::phiHasIncomingValueFromLoop(llvm::PHINode *phi, llvm::Loop *loop) {
  // If phi is from outside the loop, it is not the phi we are looking for...
  if (!loop->contains(phi) ||
    phi->getParent() != loop->getHeader()) {
    return false;
  }

  for (int i = 0; i < phi->getNumIncomingValues(); i++) {
    // Check if one the incoming edges of phi is contained in the loop
    if (loop->contains(phi->getIncomingBlock(i))) {
      return true;
    }
  }

  return false;
}

bool LoopBoundIDEAnalysis::ptrDependsOnLoopCariedPhi(llvm::Value *ptr, llvm::Loop *loop) {
  llvm::SmallVector<llvm::Value *, 32> worklist;
  llvm::DenseSet<llvm::Value *> visited;

  auto push = [&](llvm::Value *V) {
    if (!V) return;
    V = stripAddr(V);
    if (visited.insert(V).second) {
      worklist.push_back(V);
    }
  };

  worklist.push_back(ptr);
  while (!worklist.empty()) {
    llvm::Value *curr = worklist.pop_back_val();

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
      continue;
    }
  }

  return false;
}

bool LoopBoundIDEAnalysis::isMemWrittenInLoop(llvm::LoadInst *load, llvm::Loop *loop) {
  llvm::Value *ptr = load->getPointerOperand();

  for (llvm::BasicBlock *block : loop->blocks()) {
    for (llvm::Instruction &inst : *block) {
      if (auto *storeInst = llvm::dyn_cast<llvm::StoreInst>(&inst)) {
        if (stripAddr(storeInst->getPointerOperand()) == ptr) {
          return true;
        }
      }

      if (auto *callInst = llvm::dyn_cast<llvm::CallBase>(&inst)) {
        if (!callInst->onlyReadsMemory()) {
          return true;
        }
      }
    }
  }

  return false;
}

bool LoopBoundIDEAnalysis::loadIsCarriedIn(llvm::LoadInst *inst, llvm::Loop *loop) {
  if (!loop->contains(inst)) {
    return false;
  }

  llvm::Value *ptr = stripAddr(inst->getPointerOperand());

  if (ptrDependsOnLoopCariedPhi(ptr, loop)) {
    return true;
  }

  if (isMemWrittenInLoop(inst, loop)) {
    return true;
  }

  return false;
}

std::optional<int64_t> LoopBoundIDEAnalysis::findConstStepForCell(llvm::Value *Addr, llvm::Loop *L) {
  Addr = stripAddr(Addr);

  for (llvm::BasicBlock *BB : L->blocks()) {
    for (llvm::Instruction &I : *BB) {
      auto *SI = llvm::dyn_cast<llvm::StoreInst>(&I);
      if (!SI) continue;

      if (stripAddr(SI->getPointerOperand()) != Addr) continue;

      llvm::Value *Val = SI->getValueOperand();
      if (auto *BO = llvm::dyn_cast<llvm::BinaryOperator>(Val)) {
        if (BO->getOpcode() == llvm::Instruction::Add) {
          llvm::Value *Op0 = BO->getOperand(0);
          llvm::Value *Op1 = BO->getOperand(1);

          auto isLoadOfAddr = [&](llvm::Value *V) {
            if (auto *LI = llvm::dyn_cast<llvm::LoadInst>(V))
              return stripAddr(LI->getPointerOperand()) == Addr;
            return false;
          };

          if (isLoadOfAddr(Op0)) {
            if (auto *CI = llvm::dyn_cast<llvm::ConstantInt>(Op1))
              return CI->getSExtValue();
          }
          if (isLoadOfAddr(Op1)) {
            if (auto *CI = llvm::dyn_cast<llvm::ConstantInt>(Op0))
              return CI->getSExtValue();
          }
        }
      }
    }
  }
  return std::nullopt;
}

bool LoopBoundIDEAnalysis::isStoredToInLoop(llvm::Value *Addr, llvm::Loop *L) {
  Addr = stripAddr(Addr);

  for (llvm::BasicBlock *BB : L->blocks()) {
    for (llvm::Instruction &I : *BB) {
      if (auto *SI = llvm::dyn_cast<llvm::StoreInst>(&I)) {
        llvm::Value *Dst = stripAddr(SI->getPointerOperand());
        if (Dst == Addr) return true;
      }
      if (auto *CB = llvm::dyn_cast<llvm::CallBase>(&I)) {
        if (!CB->onlyReadsMemory()) return true;
      }
    }
  }
  return false;
}

std::vector<llvm::Value *> LoopBoundIDEAnalysis::sliceBackwards(llvm::Value *start, llvm::Loop *loop) {
  std::vector<llvm::Value *> roots;
  llvm::SmallVector<llvm::Value *, 32> worklist;
  llvm::DenseSet<llvm::Value *> visited;
  llvm::DenseSet<llvm::Value *> rootSet;

  auto push = [&](llvm::Value *V) {
    if (!V) return;
    if (visited.insert(V).second) {
      worklist.push_back(V);
    }
  };

  worklist.push_back(start);

  while (!worklist.empty()) {
    llvm::Value *curr = worklist.pop_back_val();
    if (!curr) {
      continue;
    }

    if (isIrrelevantToLoop(curr, loop)) {
      // If we encounter a loop invariant, we do not have to worry, as this is most likely not the counter
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
          llvm::Value *Addr = stripAddr(LI->getPointerOperand());
          // The "root" is the memory cell, not the load itself
          if (rootSet.insert(Addr).second) {
            roots.push_back(Addr);
          }
        }
        continue;
      }

      // Normal instruction inside loop
      if (loop->contains(inst)) {
        for (llvm::Value *Op : inst->operands()) {
          push(Op);
        }
      }
      continue;
    }

  }

  return roots;
}

} // namespace loopbound
