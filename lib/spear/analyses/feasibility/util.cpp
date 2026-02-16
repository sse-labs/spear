/*
 * Copyright (c) 2026 Maximilian Krebs
 * All rights reserved.
 */

#include <llvm/IR/Operator.h>
#include <llvm/IR/Instructions.h>
#include <llvm/IR/Constants.h>
#include <llvm/IR/InstrTypes.h>
#include <llvm/IR/Type.h>

#include "analyses/feasibility/FeasibilityAnalysis.h"
#include "analyses/feasibility/FeasibilityEdgeFunction.h"

namespace Feasibility::Util {


std::atomic<bool> F_DebugEnabled{true};

z3::expr mkSymBV(const llvm::Value *V, unsigned BW, const char *prefix, z3::context *Ctx) {
    std::string name;
    if (V && V->hasName()) {
        name = std::string(prefix) + "_" + V->getName().str();
    } else {
        name = std::string(prefix) + "_" + std::to_string(reinterpret_cast<uintptr_t>(V));
    }
    return Ctx->bv_const(name.c_str(), BW);
}

const llvm::Value *asValue(FeasibilityDomain::d_t fact) {
    return static_cast<const llvm::Value *>(fact);
}

const llvm::Value *stripAddr(const llvm::Value *pointer) {
    pointer = pointer->stripPointerCasts();

    while (true) {
        if (auto *gepOperator = llvm::dyn_cast<llvm::GEPOperator>(pointer)) {
            pointer = gepOperator->getPointerOperand()->stripPointerCasts();
            continue;
        }

        if (auto *operator_inst = llvm::dyn_cast<llvm::Operator>(pointer)) {
            switch (operator_inst->getOpcode()) {
                case llvm::Instruction::BitCast:
                case llvm::Instruction::AddrSpaceCast:
                    pointer = operator_inst->getOperand(0)->stripPointerCasts();
                    continue;
                default:
                    break;
            }
        }

        break;
    }

    return pointer;
}

void dumpFact(Feasibility::FeasibilityAnalysis *analysis, Feasibility::FeasibilityDomain::d_t fact) {
    if (!F_DebugEnabled.load())
        return;
    if (!fact) {
        llvm::errs() << "<null>";
        return;
    }
    if (analysis->isZeroValue(fact)) {
        llvm::errs() << "<ZERO>";
        return;
    }
    const llvm::Value *value = asValue(fact);
    const llvm::Value *strippedValue = stripAddr(value);
    llvm::errs() << value;
    if (strippedValue != value) {
        llvm::errs() << " (strip=" << strippedValue << ")";
    }
}

void dumpInst(Feasibility::FeasibilityDomain::n_t instruction) {
    if (!F_DebugEnabled.load())
        return;
    if (!instruction) {
        llvm::errs() << "<null-inst>";
        return;
    }
    llvm::errs() << *instruction;
}

void dumpEF(const Feasibility::FeasibilityAnalysis::EdgeFunctionType &edgeFunction) {
    if (edgeFunction.template isa<Feasibility::FeasibilityIdentityEF>()) {
        llvm::errs() << "EF=ID";
        return;
    }

    if (edgeFunction.template isa<Feasibility::FeasibilityAllBottomEF>() ||
        llvm::isa<psr::AllBottom<Feasibility::l_t>>(edgeFunction)) {
        llvm::errs() << "EF=BOT";
        return;
    }

    if (edgeFunction.template isa<Feasibility::FeasibilityAllTopEF>() ||
        llvm::isa<psr::AllTop<Feasibility::l_t>>(edgeFunction)) {
        llvm::errs() << "EF=TOP";
        return;
    }

    if (auto *assume = edgeFunction.template dyn_cast<Feasibility::FeasibilityAssumeEF>()) {
        llvm::errs() << "EF=ASSUME[" << assume->Cond.to_string() << "]";
        return;
    }

    if (auto *ssaset = edgeFunction.template dyn_cast<Feasibility::FeasibilitySetSSAEF>()) {
        llvm::errs() << "EF=SETSSA[" << ssaset->Key << "]";
        return;
    }

    if (auto *memsset = edgeFunction.template dyn_cast<Feasibility::FeasibilitySetMemEF>()) {
        llvm::errs() << "EF=SETMEM[" << memsset->Loc << "]";
        return;
    }

    llvm::errs() << "EF=<other>";
}

void dumpEFKind(const EF &E) {
    llvm::errs() << "[EFKind=";

    if (E.template isa<FeasibilityIdentityEF>()) {
        llvm::errs() << "FeasibilityIdentityEF";
    }
    else if (E.template isa<FeasibilityAllTopEF>()) {
        llvm::errs() << "FeasibilityAllTopEF";
    }
    else if (E.template isa<FeasibilityAllBottomEF>()) {
        llvm::errs() << "FeasibilityAllBottomEF";
    }
    else if (E.template isa<FeasibilityAssumeEF>()) {
        llvm::errs() << "FeasibilityAssumeEF";
    }
    else if (E.template isa<FeasibilitySetSSAEF>()) {
        llvm::errs() << "FeasibilitySetSSAEF";
    }
    else if (E.template isa<FeasibilitySetMemEF>()) {
        llvm::errs() << "FeasibilitySetMemEF";
    }

    else if (llvm::isa<psr::AllTop<FeasibilityAnalysis::l_t>>(E)) {
        llvm::errs() << "psr::AllTop";
    }
    else if (llvm::isa<psr::AllBottom<FeasibilityAnalysis::l_t>>(E)) {
        llvm::errs() << "psr::AllBottom";
    }
    else if (llvm::isa<psr::EdgeIdentity<FeasibilityAnalysis::l_t>>(E)) {
        llvm::errs() << "psr::EdgeIdentity";
    }

    else {
        llvm::errs() << "UNKNOWN";
    }

    llvm::errs() << "]";
}

z3::expr createFreshBitVal(const llvm::Value *key, unsigned bitwidth, const char *prefix, z3::context *context) {
    return mkSymBV(key, bitwidth, prefix, context);
}

std::optional<z3::expr> createIntVal(const llvm::Value *val, z3::context *context) {
    if (auto constval = llvm::dyn_cast<llvm::ConstantInt>(val)) {
        unsigned bitwidth = constval->getBitWidth();
        uint64_t numval = constval->getValue().getZExtValue();
        return context->bv_val(numval, bitwidth);
    }
    return std::nullopt;
}

std::optional<z3::expr> createBitVal(const llvm::Value *V, z3::context *context) {
    if (!V) {
        return std::nullopt;
    }

    if (auto C = createIntVal(V, context)) {
        return C;
    }

    if (V->getType()->isIntegerTy()) {
        unsigned bw = llvm::cast<llvm::IntegerType>(V->getType())->getBitWidth();
        return mkSymBV(V, bw, "v", context);
    }

    return std::nullopt;
}

std::optional<z3::expr> resolve(const llvm::Value *V, const FeasibilityElement &St, FeasibilityStateStore *Store) {
    if (!V || !Store) {
    return std::nullopt;
  }

  // ------------------------------------------------------------
  // 0) Constants
  // ------------------------------------------------------------
  if (auto C = createIntVal(V, &Store->ctx())) {
    return C;
  }

  if (!V->getType()->isIntegerTy()) {
    return std::nullopt;
  }

  const unsigned Bw =
      llvm::cast<llvm::IntegerType>(V->getType())->getBitWidth();

  // ------------------------------------------------------------
  // 1) SSA lookup for *any* integer-typed Value
  //    (this includes LoadInst results, BinaryOperator results, args, etc.)
  // ------------------------------------------------------------
  if (auto SsaId = Store->Ssa.getValue(St.ssaId, V)) {
    return Store->exprOf(*SsaId);
  }

  // ------------------------------------------------------------
  // 2) Load fallback: if SSA doesn't have the load result yet,
  //    read from MEM using the load's pointer operand.
  // ------------------------------------------------------------
  if (const auto *LI = llvm::dyn_cast<llvm::LoadInst>(V)) {
    const llvm::Value *Loc = LI->getPointerOperand()->stripPointerCasts();

    if (auto MemId = Store->Mem.getValue(St.memId, Loc)) {
      return Store->exprOf(*MemId);
    }

    // Unknown memory -> stable symbol for this *location* (not for the load!)
    // This is crucial: two loads from the same Loc must resolve to the same thing.
    const auto SymId = Store->getOrCreateSym(Loc, Bw, "mem");
    return Store->exprOf(SymId);
  }

  // ------------------------------------------------------------
  // 3) Casts
  // ------------------------------------------------------------
  if (const auto *Cast = llvm::dyn_cast<llvm::CastInst>(V)) {
    auto Op = resolve(Cast->getOperand(0), St, Store);
    if (!Op) {
      return std::nullopt;
    }

    const unsigned DstBW =
        llvm::cast<llvm::IntegerType>(Cast->getType())->getBitWidth();
    const unsigned SrcBW = Op->get_sort().bv_size();

    if (llvm::isa<llvm::ZExtInst>(Cast)) {
      if (DstBW > SrcBW) return z3::zext(*Op, DstBW - SrcBW);
      if (DstBW < SrcBW) return Op->extract(DstBW - 1, 0);
      return *Op;
    }
    if (llvm::isa<llvm::SExtInst>(Cast)) {
      if (DstBW > SrcBW) return z3::sext(*Op, DstBW - SrcBW);
      if (DstBW < SrcBW) return Op->extract(DstBW - 1, 0);
      return *Op;
    }
    if (llvm::isa<llvm::TruncInst>(Cast)) {
      if (DstBW <= SrcBW) return Op->extract(DstBW - 1, 0);
      return z3::zext(*Op, DstBW - SrcBW);
    }
    if (llvm::isa<llvm::BitCastInst>(Cast)) {
      if (DstBW == SrcBW) return *Op;
      if (DstBW < SrcBW) return Op->extract(DstBW - 1, 0);
      return z3::zext(*Op, DstBW - SrcBW);
    }

    return std::nullopt;
  }

  // ------------------------------------------------------------
  // 4) Binary operators
  // ------------------------------------------------------------
  if (const auto *BO = llvm::dyn_cast<llvm::BinaryOperator>(V)) {
    auto L = resolve(BO->getOperand(0), St, Store);
    auto R = resolve(BO->getOperand(1), St, Store);
    if (!L || !R) {
      return std::nullopt;
    }

    switch (BO->getOpcode()) {
      case llvm::Instruction::Add:  return (*L) + (*R);
      case llvm::Instruction::Sub:  return (*L) - (*R);
      case llvm::Instruction::Mul:  return (*L) * (*R);
      case llvm::Instruction::And:  return (*L) & (*R);
      case llvm::Instruction::Or:   return (*L) | (*R);
      case llvm::Instruction::Xor:  return (*L) ^ (*R);
      case llvm::Instruction::Shl:  return z3::shl(*L, *R);
      case llvm::Instruction::LShr: return z3::lshr(*L, *R);
      case llvm::Instruction::AShr: return z3::ashr(*L, *R);
      default: break;
    }
    return std::nullopt;
  }

  // ------------------------------------------------------------
  // 5) Fallback: stable symbol for the SSA value itself
  // ------------------------------------------------------------
  const auto SymId = Store->getOrCreateSym(V, Bw, "v");
  return Store->exprOf(SymId);
}

const llvm::Instruction* firstRealInst(const llvm::BasicBlock *BB) {
    if (!BB) {
        return nullptr;
    }
#if LLVM_VERSION_MAJOR >= 14
    return BB->getFirstNonPHIOrDbgOrLifetime();
#else
    // Fallback: skip PHI and dbg
    for (const llvm::Instruction &I : *BB) {
        if (!llvm::isa<llvm::PHINode>(&I) && !llvm::isa<llvm::DbgInfoIntrinsic>(&I)) {
            return &I;
        }
    }
    return nullptr;
#endif
}

} // namespace Feasibility::Util
