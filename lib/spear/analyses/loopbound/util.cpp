/*
 * Copyright (c) 2026 Maximilian Krebs
 * All rights reserved.
*/

#include "analyses/loopbound/util.h"

#include <llvm/IR/Operator.h>
#include <llvm/Support/raw_ostream.h>

#include <string>
#include <llvm/Analysis/ConstantFolding.h>

#include "analyses/loopbound/loopBoundWrapper.h"

namespace LoopBound::Util {

std::atomic<bool> LB_DebugEnabled{true};

const llvm::Value *asValue(LoopBound::LoopBoundDomain::d_t fact) {
  return static_cast<const llvm::Value *>(fact);
}

const llvm::Value *stripAddr(const llvm::Value *Ptr) {
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

void dumpFact(LoopBound::LoopBoundIDEAnalysis *A, LoopBound::LoopBoundDomain::d_t F) {
  if (!LB_DebugEnabled.load())
    return;
  if (!F) {
    llvm::errs() << "<null>";
    return;
  }
  if (A->isZeroValue(F)) {
    llvm::errs() << "<ZERO>";
    return;
  }
  const llvm::Value *V = asValue(F);
  const llvm::Value *S = stripAddr(V);
  llvm::errs() << V;
  if (S != V) {
    llvm::errs() << " (strip=" << S << ")";
  }
}

void dumpInst(LoopBound::LoopBoundDomain::n_t inst) {
  if (!LB_DebugEnabled.load())
    return;
  if (!inst) {
    llvm::errs() << "<null-inst>";
    return;
  }
  llvm::errs() << *inst;
}

void dumpEF(const LoopBound::EF &edgeFunction) {
  if (edgeFunction.template isa<LoopBound::DeltaIntervalIdentity>()) {
    llvm::errs() << "EF=ID";
    return;
  }

  if (edgeFunction.template isa<LoopBound::DeltaIntervalBottom>() ||
      llvm::isa<psr::AllBottom<LoopBound::l_t>>(edgeFunction)) {
    llvm::errs() << "EF=BOT";
    return;
  }

  if (auto *C = edgeFunction.template dyn_cast<LoopBound::DeltaIntervalAdditive>()) {
    llvm::errs() << "EF=ADD[" << C->lowerBound << "," << C->upperBound << "]";
    return;
  }

  if (auto *C = edgeFunction.template dyn_cast<LoopBound::DeltaIntervalMultiplicative>()) {
    llvm::errs() << "EF=MUL[" << C->lowerBound << "," << C->upperBound << "]";
    return;
  }

  if (auto *C = edgeFunction.template dyn_cast<LoopBound::DeltaIntervalDivision>()) {
    llvm::errs() << "EF=DIV[" << C->lowerBound << "," << C->upperBound << "]";
    return;
  }

  llvm::errs() << "EF=<other>";
}

const llvm::Value *getUnderlyingObject(const llvm::Value *Ptr) {
  if (!Ptr) {
    return nullptr;
  }

  const llvm::Value *Cur = Ptr;

  while (Cur) {
    Cur = Cur->stripPointerCasts();

    // Peel GEPs (instruction + operator forms)
    if (auto *GEP = llvm::dyn_cast<llvm::GetElementPtrInst>(Cur)) {
      Cur = GEP->getPointerOperand();
      continue;
    }
    if (auto *GEP = llvm::dyn_cast<llvm::GEPOperator>(Cur)) {
      Cur = GEP->getPointerOperand();
      continue;
    }

    break;
  }

  return Cur;
}

const llvm::ConstantInt *tryEvalToConstInt(const llvm::Value *val) {
  if (!val) {
    return nullptr;
  }

  if (auto *CI = llvm::dyn_cast<llvm::ConstantInt>(val)) {
    return CI;
  }

  // Handle integer casts of constants
  if (auto *Cast = llvm::dyn_cast<llvm::CastInst>(val)) {
    const llvm::ConstantInt *Inner = tryEvalToConstInt(Cast->getOperand(0));
    if (!Inner) {
      return nullptr;
    }
    if (!Cast->getType()->isIntegerTy()) {
      return nullptr;
    }

    llvm::APInt Val = Inner->getValue();
    unsigned W = Cast->getType()->getIntegerBitWidth();

    switch (Cast->getOpcode()) {
    case llvm::Instruction::ZExt:
      Val = Val.zext(W);
      break;
    case llvm::Instruction::SExt:
      Val = Val.sext(W);
      break;
    case llvm::Instruction::Trunc:
      Val = Val.trunc(W);
      break;
    default:
      return nullptr;
    }

    return llvm::ConstantInt::get(Cast->getType()->getContext(), Val);
  }

  // Fold simple binops of constant ints (recursively)
  auto *BO = llvm::dyn_cast<llvm::BinaryOperator>(val);
  if (!BO) {
    return nullptr;
  }

  const llvm::ConstantInt *C0 = tryEvalToConstInt(BO->getOperand(0));
  const llvm::ConstantInt *C1 = tryEvalToConstInt(BO->getOperand(1));
  if (!C0 || !C1) {
    return nullptr;
  }

  llvm::APInt A = C0->getValue();
  llvm::APInt B = C1->getValue();
  llvm::APInt R(A.getBitWidth(), 0);

  switch (BO->getOpcode()) {
  case llvm::Instruction::Add:
    R = A + B;
    break;
  case llvm::Instruction::Sub:
    R = A - B;
    break;
  case llvm::Instruction::Mul:
    R = A * B;
    break;
  case llvm::Instruction::And:
    R = A & B;
    break;
  case llvm::Instruction::Or:
    R = A | B;
    break;
  case llvm::Instruction::Xor:
    R = A ^ B;
    break;
  case llvm::Instruction::Shl:
    R = A.shl(B);
    break;
  case llvm::Instruction::LShr:
    R = A.lshr(B);
    break;
  case llvm::Instruction::AShr:
    R = A.ashr(B);
    break;
  case llvm::Instruction::UDiv:
    if (B == 0)
      return nullptr;
    R = A.udiv(B);
    break;
  case llvm::Instruction::SDiv:
    if (B == 0)
      return nullptr;
    R = A.sdiv(B);
    break;
  default:
    return nullptr;
  }

  return llvm::ConstantInt::get(BO->getType()->getContext(), R);
}

const llvm::StoreInst *findDominatingStoreToObject(const llvm::LoadInst *LI,
                                                   const llvm::Value *Obj,
                                                   llvm::DominatorTree &DT) {
  if (!LI || !Obj) {
    return nullptr;
  }

  const llvm::StoreInst *Best = nullptr;

  const llvm::Function *F = LI->getFunction();
  if (!F) {
    return nullptr;
  }

  for (const llvm::BasicBlock &BB : *F) {
    for (const llvm::Instruction &I : BB) {
      auto *SI = llvm::dyn_cast<llvm::StoreInst>(&I);
      if (!SI) {
        continue;
      }

      const llvm::Value *StoreObj = getUnderlyingObject(SI->getPointerOperand());
      if (!StoreObj || StoreObj != Obj) {
        continue;
      }

      if (!DT.dominates(SI, LI)) {
        continue;
      }

      if (!Best) {
        Best = SI;
        continue;
      }

      // Prefer a later dominating store if dominance-ordered
      if (DT.dominates(Best, SI)) {
        Best = SI;
        continue;
      }

      // Same block: pick whichever appears later before the load
      if (SI->getParent() == LI->getParent() && Best->getParent() == LI->getParent()) {
        const llvm::BasicBlock *B = LI->getParent();
        const llvm::StoreInst *LastSeen = nullptr;
        for (const llvm::Instruction &J : *B) {
          if (&J == Best)
            LastSeen = Best;
          if (&J == SI)
            LastSeen = SI;
          if (&J == LI)
            break;
        }
        Best = LastSeen;
      } else if (SI->getParent() == LI->getParent() && Best->getParent() != LI->getParent()) {
        Best = SI;
      }
    }
  }

  return Best;
}

const llvm::ICmpInst *peelToICmp(const llvm::Value *V) {
  if (!V) {
    return nullptr;
  }

  const llvm::Value *Cur = V;

  // strip casts
  while (auto *CI = llvm::dyn_cast<llvm::CastInst>(Cur)) {
    Cur = CI->getOperand(0);
  }

  // handle xor with true as logical NOT
  if (auto *BO = llvm::dyn_cast<llvm::BinaryOperator>(Cur)) {
    if (BO->getOpcode() == llvm::Instruction::Xor) {
      if (auto *C = llvm::dyn_cast<llvm::ConstantInt>(BO->getOperand(1))) {
        if (C->isOne()) {
          Cur = BO->getOperand(0);
          while (auto *CI2 = llvm::dyn_cast<llvm::CastInst>(Cur)) {
            Cur = CI2->getOperand(0);
          }
        }
      }
    }
  }

  return llvm::dyn_cast<llvm::ICmpInst>(Cur);
}

const llvm::Value *getMemRootFromValue(const llvm::Value *V) {
  if (!V) {
    return nullptr;
  }

  const llvm::Value *Cur = V;

  while (auto *CI = llvm::dyn_cast<llvm::CastInst>(Cur)) {
    Cur = CI->getOperand(0);
  }

  if (auto *LI = llvm::dyn_cast<llvm::LoadInst>(Cur)) {
    const llvm::Value *P = LI->getPointerOperand();
    return stripAddr(P);
  }

  if (auto *GEP = llvm::dyn_cast<llvm::GetElementPtrInst>(Cur)) {
    const llvm::Value *P = GEP;
    return stripAddr(P);
  }

  return nullptr;
}

std::optional<int64_t> tryDeduceConstFromLoad(
const llvm::LoadInst *LI, llvm::DominatorTree &DT, llvm::LoopInfo &LIInfo) {
  if (!LI) return std::nullopt;

  const llvm::Value *Obj = getUnderlyingObject(LI->getPointerOperand());
  if (!Obj) return std::nullopt;

  // If Obj is written in any loop that can affect this load, do not treat it as const.
  // In particular: if there is a store to Obj in any loop that contains this load
  // (or any parent loop), it's loop-variant.
  llvm::Loop *L = LIInfo.getLoopFor(LI->getParent());
  if (L) {
    for (llvm::Loop *Cur = L; Cur != nullptr; Cur = Cur->getParentLoop()) {
      for (llvm::BasicBlock *BB : Cur->blocks()) {
        for (llvm::Instruction &I : *BB) {
          auto *SI = llvm::dyn_cast<llvm::StoreInst>(&I);
          if (!SI) continue;
          const llvm::Value *Dst = stripAddr(SI->getPointerOperand());
          if (Dst == Obj) {
            return std::nullopt;  // loop-variant → not const
          }
        }
      }
    }
  }

  const llvm::StoreInst *Def = findDominatingStoreToObject(LI, Obj, DT);
  if (!Def) return std::nullopt;

  const llvm::ConstantInt *C = tryEvalToConstInt(Def->getValueOperand());
  if (!C) return std::nullopt;

  return C->getSExtValue();
}

std::string predicateToSymbol(llvm::CmpInst::Predicate P) {
  if (P) {
    switch (P) {
    case llvm::CmpInst::ICMP_EQ:
      return "==";
    case llvm::CmpInst::ICMP_NE:
      return "!=";
    case llvm::CmpInst::ICMP_UGT:
      return ">";
    case llvm::CmpInst::ICMP_ULT:
      return "<";
    case llvm::CmpInst::ICMP_UGE:
      return ">=";
    case llvm::CmpInst::ICMP_ULE:
      return "<=";
    case llvm::CmpInst::ICMP_SGT:
      return ">";
    case llvm::CmpInst::ICMP_SLT:
      return "<";
    case llvm::CmpInst::ICMP_SGE:
      return ">=";
    case llvm::CmpInst::ICMP_SLE:
      return "<=";
    default:
      return "UNKNOWN PREDICATE";
    }
  }
  return "UNDEFINED";
}

llvm::CmpInst::Predicate flipPredicate(llvm::CmpInst::Predicate predicate) {
  switch (predicate) {
    case llvm::CmpInst::Predicate::ICMP_SLT: return llvm::CmpInst::Predicate::ICMP_SGT;
    case llvm::CmpInst::Predicate::ICMP_SLE: return llvm::CmpInst::Predicate::ICMP_SGE;
    case llvm::CmpInst::Predicate::ICMP_SGT: return llvm::CmpInst::Predicate::ICMP_SLT;
    case llvm::CmpInst::Predicate::ICMP_SGE: return llvm::CmpInst::Predicate::ICMP_SLE;

    case llvm::CmpInst::Predicate::ICMP_ULT: return llvm::CmpInst::Predicate::ICMP_UGT;
    case llvm::CmpInst::Predicate::ICMP_ULE: return llvm::CmpInst::Predicate::ICMP_UGE;
    case llvm::CmpInst::Predicate::ICMP_UGT: return llvm::CmpInst::Predicate::ICMP_ULT;
    case llvm::CmpInst::Predicate::ICMP_UGE: return llvm::CmpInst::Predicate::ICMP_ULE;

    default: return predicate;
  }
}

int64_t floorDiv(int64_t a, int64_t b) {
  int64_t q = a / b;
  int64_t r = a % b;
  if (r != 0 && ((r > 0) != (b > 0))) {
    --q;
  }
  return q;
}

int64_t ceilDiv(int64_t a, int64_t b) {
  int64_t q = a / b;
  int64_t r = a % b;
  if (r != 0 && ((r > 0) == (b > 0))) {
    ++q;
  }
  return q;
}

int64_t exactDiv(int64_t a, int64_t b) {
  if (a == 0 || b == 0) {
    return 0;
  }

  return a / b;
}

const llvm::Value *stripCasts(const llvm::Value *V) {
  while (auto *C = llvm::dyn_cast<llvm::CastInst>(V)) {
    V = C->getOperand(0);
  }
  return V;
}

bool predicatesCoditionHolds(llvm::CmpInst::Predicate pred, int64_t val, int64_t check) {
  switch (pred) {
    // signed
    case llvm::CmpInst::ICMP_SLT: return val <  check;
    case llvm::CmpInst::ICMP_SLE: return val <= check;
    case llvm::CmpInst::ICMP_SGT: return val >  check;
    case llvm::CmpInst::ICMP_SGE: return val >= check;

      // unsigned
    case llvm::CmpInst::ICMP_ULT: return static_cast<uint64_t>(val) <  static_cast<uint64_t>(check);
    case llvm::CmpInst::ICMP_ULE: return static_cast<uint64_t>(val) <= static_cast<uint64_t>(check);
    case llvm::CmpInst::ICMP_UGT: return static_cast<uint64_t>(val) >  static_cast<uint64_t>(check);
    case llvm::CmpInst::ICMP_UGE: return static_cast<uint64_t>(val) >= static_cast<uint64_t>(check);

    default:
      return false;
  }
}

bool loopIsUniform(llvm::Loop *L, llvm::DominatorTree &DT) {
  if (!L) return false;

  // Still important for your init/inc inference
  llvm::BasicBlock *PreH  = L->getLoopPreheader();
  llvm::BasicBlock *Latch = L->getLoopLatch();
  if (!PreH || !Latch) return false;

  // We need at least one "main" exiting condition that bounds the backedge.
  // Prefer: condition on latch terminator
  auto *LatchBr = llvm::dyn_cast<llvm::BranchInst>(Latch->getTerminator());
  if (LatchBr && LatchBr->isConditional()) {
    if (LoopBound::Util::peelToICmp(LatchBr->getCondition())) {
      return true;
    }
  }

  // Otherwise: find an exiting block whose terminator condition is an ICmp
  // and which dominates the latch (so it constrains the backedge count).
  llvm::SmallVector<llvm::BasicBlock*, 8> ExitingBlocks;
  L->getExitingBlocks(ExitingBlocks);

  for (llvm::BasicBlock *EB : ExitingBlocks) {
    auto *BI = llvm::dyn_cast<llvm::BranchInst>(EB->getTerminator());
    if (!BI || !BI->isConditional()) continue;

    const llvm::ICmpInst *IC = LoopBound::Util::peelToICmp(BI->getCondition());
    if (!IC) continue;

    // If this test dominates the latch, it bounds how often we can reach the latch.
    if (DT.dominates(EB, Latch)) {
      return true;
    }
  }

  return false;
}


static const llvm::LoadInst *getDirectLoadFromRoot(const llvm::Value *V,
                                                   const llvm::Value *Root) {
  if (!V || !Root) return nullptr;

  V = LoopBound::Util::stripCasts(V);

  // Accept add/sub with 0 around the load (common noise)
  if (auto *BO = llvm::dyn_cast<llvm::BinaryOperator>(V)) {
    auto opc = BO->getOpcode();
    if (opc == llvm::Instruction::Add || opc == llvm::Instruction::Sub) {
      const llvm::Value *A = LoopBound::Util::stripCasts(BO->getOperand(0));
      const llvm::Value *B = LoopBound::Util::stripCasts(BO->getOperand(1));

      // x + 0 or x - 0
      if (auto *CI = llvm::dyn_cast<llvm::ConstantInt>(B)) {
        if (CI->isZero()) {
          V = A;
        }
      }
    }
  }

  auto *LI = llvm::dyn_cast<llvm::LoadInst>(V);
  if (!LI) return nullptr;

  const llvm::Value *Ptr = LoopBound::Util::stripAddr(LI->getPointerOperand());
  return (Ptr == Root) ? LI : nullptr;
}


bool loopConditionCannotBeDeduced(LoopBound::LoopParameterDescription description,
                                  llvm::FunctionAnalysisManager *FAM,
                                  llvm::DominatorTree &DT,
                                  llvm::LoopInfo &LIInfo) {
  auto checkexpr = LoopBoundWrapper::findLoopCheckExpr(description, FAM, LIInfo);
  if (checkexpr.has_value()) {
    auto bound = checkexpr.value().calculateCheck(FAM, LIInfo);
    return bound.has_value();
  }
  return true;
}

bool loopInitCannotBeDeduced(LoopBound::LoopParameterDescription description) {
  return !description.init.has_value();
}

bool loopIsCounting(llvm::Loop *loop, llvm::ICmpInst *IC) {
  if (!loop || !IC) return false;

  auto info = LoopBoundIDEAnalysis::findCounterFromICMP(IC, loop);
  if (!info || info->Roots.empty()) {
    return false; // no counter candidate
  }

  const llvm::Value *Root = LoopBound::Util::stripAddr(info->Roots[0]);

  // Look for a store to that root inside the loop that we can parse as increment
  bool foundIncrementStore = false;

  for (llvm::BasicBlock *BB : loop->blocks()) {
    for (llvm::Instruction &I : *BB) {
      auto *SI = llvm::dyn_cast<llvm::StoreInst>(&I);
      if (!SI) continue;

      // must store to the same root object
      const llvm::Value *Dst = LoopBound::Util::stripAddr(SI->getPointerOperand());
      if (Dst != Root) continue;

      // can we extract a constant increment/mul/div etc?
      if (LoopBoundIDEAnalysis::extractConstIncFromStore(SI, Root).has_value()) {
        foundIncrementStore = true;
        break;
      }
    }
    if (foundIncrementStore) break;
  }

  return foundIncrementStore;
}

static bool isMemoryRootWrittenInLoop(const llvm::Value *Base,
                                     llvm::Loop *L) {
  if (!Base || !L) return false;

  const llvm::Value *NormBase = LoopBound::Util::stripAddr(Base);

  for (llvm::BasicBlock *BB : L->blocks()) {
    for (llvm::Instruction &I : *BB) {
      auto *SI = llvm::dyn_cast<llvm::StoreInst>(&I);
      if (!SI) continue;

      const llvm::Value *Dst =
          LoopBound::Util::stripAddr(SI->getPointerOperand());
      if (Dst == NormBase) {
        return true;
      }
    }
  }
  return false;
}

bool loopIsDependentNested(const LoopParameterDescription &desc,
                           llvm::LoopInfo &LIInfo) {
  if (!desc.loop || !desc.icmp || !desc.counterRoot) {
    return false;
  }

  llvm::Loop *L = desc.loop;
  llvm::Loop *Parent = L->getParentLoop();
  if (!Parent) {
    return false; // not nested
  }

  const llvm::Value *CounterRoot =
      LoopBound::Util::stripAddr(desc.counterRoot);

  const llvm::Value *Op0 =
      LoopBound::Util::stripCasts(desc.icmp->getOperand(0));
  const llvm::Value *Op1 =
      LoopBound::Util::stripCasts(desc.icmp->getOperand(1));

  auto norm = [](const llvm::Value *V) -> const llvm::Value * {
    if (!V) return nullptr;
    return LoopBound::Util::stripAddr(
             LoopBound::Util::stripCasts(V));
  };

  auto E0 = LoopBoundWrapper::peelBasePlusConst(Op0);
  auto E1 = LoopBoundWrapper::peelBasePlusConst(Op1);

  auto isCounterExpr = [&](const std::optional<CheckExpr> &E) -> bool {
    if (!E || E->isConstant) return false;
    return norm(E->Base) == CounterRoot;
  };

  const bool op0IsCounterSide = isCounterExpr(E0);
  const bool op1IsCounterSide = isCounterExpr(E1);

  const llvm::Value *BoundV = nullptr;

  if (op0IsCounterSide && !op1IsCounterSide) {
    BoundV = Op1;
  } else if (!op0IsCounterSide && op1IsCounterSide) {
    BoundV = Op0;
  } else {
    // ambiguous or malformed -> don't classify as dependent nested
    return false;
  }

  auto BoundExpr = LoopBoundWrapper::peelBasePlusConst(BoundV);
  if (!BoundExpr || BoundExpr->isConstant || !BoundExpr->Base) {
    return false; // constant or unbased bound
  }

  const llvm::Value *BoundBase = norm(BoundExpr->Base);

  // Check all enclosing loops
  for (llvm::Loop *P = Parent; P != nullptr; P = P->getParentLoop()) {
    if (isMemoryRootWrittenInLoop(BoundBase, P)) {
      return true;
    }
  }

  return false;
}


LoopBound::LoopType determineLoopType(LoopBound::LoopParameterDescription description, llvm::FunctionAnalysisManager *FAM) {
  auto *preheader = description.loop->getLoopPreheader();
  auto *function = preheader->getParent();

  auto &domTree = FAM->getResult<llvm::DominatorTreeAnalysis>(*function);
  auto &LIInfo = FAM->getResult<llvm::LoopAnalysis>(*function);

  llvm::errs() << description.loop->getName() << "\n";

  // Check if loop is non uniform
  if (!loopIsUniform(description.loop, domTree)) {
    return LoopType::MALFORMED_LOOP;
  }

  if (loopIsDependentNested(description, LIInfo)) {
    return LoopType::NESTED_LOOP;
  }

  if (!loopConditionCannotBeDeduced(description, FAM, domTree, LIInfo)) {
    return LoopType::SYMBOLIC_BOUND_LOOP;
  }

  if (loopInitCannotBeDeduced(description)) {
    return LoopType::SYMBOLIC_BOUND_LOOP;
  }

  if (!loopIsCounting(description.loop, description.icmp)) {
    return LoopType::NON_COUNTING_LOOP;
  }

  return LoopType::NORMAL_LOOP;
}

std::string LoopTypeToString(LoopBound::LoopType type) {
  switch (type) {
    case LoopBound::LoopType::NORMAL_LOOP:
      return "NORMAL_LOOP";
    case LoopType::MALFORMED_LOOP:
      return "MALFORMED_LOOP";
    case LoopType::SYMBOLIC_BOUND_LOOP:
      return "SYMBOLIC_BOUND_LOOP";
    case LoopType::NON_COUNTING_LOOP:
      return "NON_COUNTING_LOOP";
    case LoopType::NESTED_LOOP:
      return "NESTED_LOOP";
    case LoopType::UNKNOWN_LOOP:
      return "UNKNOWN_LOOP";
    default:
      return "";
  }
}

}  // namespace LoopBound::Util
