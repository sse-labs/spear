// /*
//  * Copyright (c) 2026 Maximilian Krebs
//  * All rights reserved.
// *

//
// Created by max on 1/28/26.
//

#ifndef SPEAR_UTIL_H
#define SPEAR_UTIL_H

#include <atomic>
#include <optional>
#include <type_traits>

#include <llvm/IR/Constants.h>
#include <llvm/IR/Dominators.h>
#include <llvm/IR/Instructions.h>
#include <llvm/IR/Operator.h>
#include <llvm/IR/Value.h>

#include "loopbound.h"
#include "LoopBoundEdgeFunction.h"

namespace loopbound {

static std::atomic<bool> LB_DebugEnabled{true};
static constexpr const char *LB_TAG = "[LBDBG]";

static inline const llvm::Value *asValue(loopbound::LoopBoundIDEAnalysis::d_t F) {
  return static_cast<const llvm::Value *>(F);
}

static inline void dumpFact(loopbound::LoopBoundIDEAnalysis *A,
                            loopbound::LoopBoundIDEAnalysis::d_t F) {
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
  const llvm::Value *S = A->stripAddr(V);
  llvm::errs() << V;
  if (S != V) {
    llvm::errs() << " (strip=" << S << ")";
  }
}

static inline void dumpInst(loopbound::LoopBoundIDEAnalysis::n_t I) {
  if (!LB_DebugEnabled.load())
    return;
  if (!I) {
    llvm::errs() << "<null-inst>";
    return;
  }
  llvm::errs() << *I;
}

inline void dumpEF(const loopbound::EF &E) {
  if (E.template isa<loopbound::DeltaIntervalIdentity>()) {
    llvm::errs() << "EF=ID";
    return;
  }
  if (E.template isa<loopbound::DeltaIntervalBottom>() ||
      llvm::isa<psr::AllBottom<loopbound::l_t>>(E)) {
    llvm::errs() << "EF=BOT";
    return;
  }
  if (E.template isa<loopbound::DeltaIntervalTop>() ||
      llvm::isa<psr::AllTop<loopbound::l_t>>(E)) {
    llvm::errs() << "EF=TOP";
    return;
  }
  if (auto *N = E.template dyn_cast<loopbound::DeltaIntervalNormal>()) {
    llvm::errs() << "EF=ADD[" << N->lowerBound << "," << N->upperBound << "]";
    return;
  }
  if (auto *C = E.template dyn_cast<loopbound::DeltaIntervalCollect>()) {
    llvm::errs() << "EF=COLLECT[" << C->lowerBound << "," << C->upperBound << "]";
    return;
  }
  if (auto *A = E.template dyn_cast<loopbound::DeltaIntervalAssign>()) {
    llvm::errs() << "EF=ASSIGN[" << A->lowerBound << "," << A->upperBound << "]";
    return;
  }

  llvm::errs() << "EF=<other>";
}

// ============================================================================
// Helpers for "deduce constant from load" / "find compare roots"
// NOTE: removed the broken overloads and made signatures consistent.
// ============================================================================

// Single underlying-object implementation (no recursion, no duplicate symbol).
inline const llvm::Value *getUnderlyingObject(const llvm::Value *Ptr) {
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

// Signature fixed: now 1-arg (no DataLayout needed) and all call sites match.
inline const llvm::ConstantInt *tryEvalToConstInt(const llvm::Value *V) {
  if (!V) {
    return nullptr;
  }

  if (auto *CI = llvm::dyn_cast<llvm::ConstantInt>(V)) {
    return CI;
  }

  // Handle integer casts of constants
  if (auto *Cast = llvm::dyn_cast<llvm::CastInst>(V)) {
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
  auto *BO = llvm::dyn_cast<llvm::BinaryOperator>(V);
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

inline const llvm::StoreInst *
findDominatingStoreToObject(const llvm::LoadInst *LI, const llvm::Value *Obj,
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
      if (SI->getParent() == LI->getParent() &&
          Best->getParent() == LI->getParent()) {
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
      } else if (SI->getParent() == LI->getParent() &&
                 Best->getParent() != LI->getParent()) {
        Best = SI;
      }
    }
  }

  return Best;
}

inline const llvm::ICmpInst *peelToICmp(const llvm::Value *V) {
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
          while (auto *CI = llvm::dyn_cast<llvm::CastInst>(Cur)) {
            Cur = CI->getOperand(0);
          }
        }
      }
    }
  }

  return llvm::dyn_cast<llvm::ICmpInst>(Cur);
}

inline const llvm::Value *getMemRootFromValue(const llvm::Value *V) {
  if (!V) {
    return nullptr;
  }

  const llvm::Value *Cur = V;

  while (auto *CI = llvm::dyn_cast<llvm::CastInst>(Cur)) {
    Cur = CI->getOperand(0);
  }

  if (auto *LI = llvm::dyn_cast<llvm::LoadInst>(Cur)) {
    const llvm::Value *P = LI->getPointerOperand();
    return loopbound::LoopBoundIDEAnalysis::stripAddr(P);
  }

  if (auto *GEP = llvm::dyn_cast<llvm::GetElementPtrInst>(Cur)) {
    const llvm::Value *P = GEP;
    return LoopBoundIDEAnalysis::stripAddr(P);
  }

  return nullptr;
}

inline std::optional<int64_t> tryDeduceConstFromLoad(const llvm::LoadInst *LI,
                                                     llvm::DominatorTree &DT) {
  if (!LI) {
    return std::nullopt;
  }

  const llvm::Value *Obj = getUnderlyingObject(LI->getPointerOperand());
  if (!Obj) {
    return std::nullopt;
  }

  const llvm::StoreInst *Def = findDominatingStoreToObject(LI, Obj, DT);
  if (!Def) {
    return std::nullopt;
  }

  const llvm::ConstantInt *C = tryEvalToConstInt(Def->getValueOperand());
  if (!C) {
    return std::nullopt;
  }

  return C->getSExtValue();
}


inline std::string predicateToSymbol(llvm::CmpInst::Predicate *P) {
  if (P) {
    switch (*P) {
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
  } else {
    return "UNDEFINED";
  }
}

} // namespace loopbound

#endif // SPEAR_UTIL_H
