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
        llvm::errs() << "EF=SETSSA[" << ssaset->ValueExpr.to_string() << "]";
        return;
    }

    if (auto *memsset = edgeFunction.template dyn_cast<Feasibility::FeasibilitySetMemEF>()) {
        llvm::errs() << "EF=SETMEM[" << memsset->ValueExpr.to_string() << "]";
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

    // --- Phasar framework EFs ---
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

// Helper: stable-ish symbolic BV for a value.
z3::expr mkSymBV(const llvm::Value *V, unsigned BW, const char *prefix, z3::context *Ctx) {
    std::string name;
    if (V && V->hasName()) {
        name = std::string(prefix) + "_" + V->getName().str();
    } else {
        name = std::string(prefix) + "_" + std::to_string(reinterpret_cast<uintptr_t>(V));
    }
    return Ctx->bv_const(name.c_str(), BW);
}

z3::expr createFreshBitVal(const llvm::Value *key, unsigned bitwidth, const char *prefix, z3::context *context) {
    return mkSymBV(key, bitwidth, prefix, context);
}

std::optional<z3::expr> createIntVal(const llvm::Value *val, z3::context *context) {
    if (auto constval = llvm::dyn_cast<llvm::ConstantInt>(val)) {
        unsigned bitwidth = constval->getBitWidth();
        // NOTE: zext to u64 is still fine for <=64bit; larger ints will get truncated here.
        // If you need true >64, switch to string-based bv_val.
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

std::optional<z3::expr> resolve(const llvm::Value *variable, FeasibilityStateStore *store) {

    if (!variable) {
        return std::nullopt;
    }

    // Constants
    if (auto C = createIntVal(variable, &store->ctx())) {
        return C;
    }

    // Loads: best-effort symbolic (until you wire store lookups by (memId, ptr) into the store API)
    if (const auto *loadInst = llvm::dyn_cast<llvm::LoadInst>(variable)) {
        if (!loadInst->getType()->isIntegerTy()) {
            return std::nullopt;
        }
        unsigned bw = llvm::cast<llvm::IntegerType>(loadInst->getType())->getBitWidth();
        return mkSymBV(loadInst, bw, "load", &store->ctx());
    }

    // Casts on integer types (BV modeling)
    if (const auto *Cast = llvm::dyn_cast<llvm::CastInst>(variable)) {
        auto op = resolve(Cast->getOperand(0), store);
        if (!op) return std::nullopt;

        if (!Cast->getType()->isIntegerTy()) return std::nullopt;
        unsigned dstBW = llvm::cast<llvm::IntegerType>(Cast->getType())->getBitWidth();
        unsigned srcBW = op->get_sort().bv_size();

        if (llvm::isa<llvm::ZExtInst>(Cast)) {
            if (dstBW > srcBW) return z3::zext(*op, dstBW - srcBW);
            if (dstBW < srcBW) return op->extract(dstBW - 1, 0);
            return *op;
        }
        if (llvm::isa<llvm::SExtInst>(Cast)) {
            if (dstBW > srcBW) return z3::sext(*op, dstBW - srcBW);
            if (dstBW < srcBW) return op->extract(dstBW - 1, 0);
            return *op;
        }
        if (llvm::isa<llvm::TruncInst>(Cast)) {
            if (dstBW <= srcBW) return op->extract(dstBW - 1, 0);
            return z3::zext(*op, dstBW - srcBW);
        }
        if (llvm::isa<llvm::BitCastInst>(Cast)) {
            if (dstBW == srcBW) return *op;
            if (dstBW < srcBW) return op->extract(dstBW - 1, 0);
            return z3::zext(*op, dstBW - srcBW);
        }

        return std::nullopt;
    }

    // Binary operators on integer types
    if (const auto *BO = llvm::dyn_cast<llvm::BinaryOperator>(variable)) {
        if (!BO->getType()->isIntegerTy()) return std::nullopt;

        auto lhs = resolve(BO->getOperand(0), store);
        auto rhs = resolve(BO->getOperand(1), store);
        if (!lhs || !rhs) return std::nullopt;

        switch (BO->getOpcode()) {
            case llvm::Instruction::Add:  return (*lhs) + (*rhs);
            case llvm::Instruction::Sub:  return (*lhs) - (*rhs);
            case llvm::Instruction::Mul:  return (*lhs) * (*rhs);
            case llvm::Instruction::And:  return (*lhs) & (*rhs);
            case llvm::Instruction::Or:   return (*lhs) | (*rhs);
            case llvm::Instruction::Xor:  return (*lhs) ^ (*rhs);
            case llvm::Instruction::Shl:  return z3::shl(*lhs, *rhs);
            case llvm::Instruction::LShr: return z3::lshr(*lhs, *rhs);
            case llvm::Instruction::AShr: return z3::ashr(*lhs, *rhs);
            default: break;
        }
        return std::nullopt;
    }

    // Generic integer value => symbolic
    if (variable->getType()->isIntegerTy()) {
        unsigned bw = llvm::cast<llvm::IntegerType>(variable->getType())->getBitWidth();
        return mkSymBV(variable, bw, "v", &store->ctx());
    }

    return std::nullopt;
}

std::string stableName(const llvm::Value *V) {
    if (!V) {
        return "null";
    }

    // 1) Named values (best case)
    if (V->hasName()) {
        return V->getName().str();
    }

    // 2) Instructions without name → use function + instruction index
    if (auto *I = llvm::dyn_cast<llvm::Instruction>(V)) {
        const llvm::Function *F = I->getFunction();

        std::string S;
        llvm::raw_string_ostream OS(S);

        if (F) {
            OS << F->getName() << "_";
        }

        // Use psr.id metadata if available (more stable than numbering)
        if (I->hasMetadata("psr.id")) {
            if (auto *MD = I->getMetadata("psr.id")) {
                if (auto *CMD = llvm::dyn_cast<llvm::ConstantAsMetadata>(MD->getOperand(0))) {
                    if (auto *CI = llvm::dyn_cast<llvm::ConstantInt>(CMD->getValue())) {
                        OS << "id" << CI->getZExtValue();
                        return OS.str();
                    }
                }
            }
        }

        // Fallback: instruction index in function
        unsigned idx = 0;
        for (const auto &BB : *F) {
            for (const auto &Inst : BB) {
                if (&Inst == I) {
                    OS << "inst" << idx;
                    return OS.str();
                }
                ++idx;
            }
        }

        OS << "inst_unknown";
        return OS.str();
    }

    // 3) Constants
    if (auto *CI = llvm::dyn_cast<llvm::ConstantInt>(V)) {
        std::string S;
        llvm::raw_string_ostream OS(S);
        OS << "c_" << CI->getValue();
        return OS.str();
    }

    if (llvm::isa<llvm::ConstantPointerNull>(V)) {
        return "null_ptr";
    }

    // 4) Fallback: print IR representation (stable across runs)
    std::string S;
    llvm::raw_string_ostream OS(S);
    V->print(OS);
    OS.flush();

    // Remove spaces/newlines (Z3 symbol must be clean)
    for (char &c : S) {
        if (c == ' ' || c == '\n' || c == '\t') {
            c = '_';
        }
    }

    return S;
}

} // namespace Feasibility::Util
