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

    if (auto *addcons = edgeFunction.template dyn_cast<Feasibility::FeasibilityAddConstrainEF>()) {
        auto mananger = addcons->manager;

        auto constraintId = addcons->pathConditionId;
        auto formular = mananger->getExpression(constraintId);

        llvm::errs() << "EF=ADDCONS[" << formular.to_string() << ")]";
        return;
    }

    llvm::errs() << "EF=<other>";
}

void dumpEFKind(const EF &E) {
    llvm::errs() << "[EFKind=";

   if (E.template isa<FeasibilityAllTopEF>()) {
        llvm::errs() << "FeasibilityAllTopEF";
    } else if (E.template isa<FeasibilityAllBottomEF>()) {
        llvm::errs() << "FeasibilityAllBottomEF";
    } else if (E.template isa<FeasibilityAddConstrainEF>()) {
        llvm::errs() << "FeasibilityAddConstrainEF";
    } else if (llvm::isa<psr::AllTop<FeasibilityAnalysis::l_t>>(E)) {
        llvm::errs() << "psr::AllTop";
    } else if (llvm::isa<psr::AllBottom<FeasibilityAnalysis::l_t>>(E)) {
        llvm::errs() << "psr::AllBottom";
    } else if (llvm::isa<psr::EdgeIdentity<FeasibilityAnalysis::l_t>>(E)) {
        llvm::errs() << "psr::EdgeIdentity";
    }

    else {
        llvm::errs() << "UNKNOWN";
    }

    llvm::errs() << "]";
}

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

uint32_t findOrAddFormulaId(FeasibilityAnalysisManager *manager, z3::expr formula) {
    auto potentialid = manager->findFormulaId(formula);
    if (!potentialid.has_value()) {
        return manager->mkAtomic(formula);
    }
    return potentialid.value();
}

z3::expr createConstraintFromICmp(FeasibilityAnalysisManager *manager, const llvm::ICmpInst* ICmp, bool areWeInTheTrueBranch) {
    auto op0 = ICmp->getOperand(0);
    auto op1 = ICmp->getOperand(1);

    auto c0 = createBitVal(op0, manager->Context);
    auto c1 = createBitVal(op1, manager->Context);

    if (!c0 || !c1) {
        // If we cannot create a formula for one of the operands,
        // we return a default formula (true) that does not constrain the analysis.
        llvm::errs() << "WARNING: Could not create constraint from ICmp instruction " << *ICmp << " because we could not create formulas for its operands.\n";
        return manager->Context->bool_val(true);
    }

    // Init the constraint formula to true, and then update it based on the predicate of the ICmp instruction.
    // We use the appropriate Z3 operators for each predicate to create the correct constraint formula.
    z3::expr cmp = manager->Context->bool_val(true);
    switch (ICmp->getPredicate()) {
        case llvm::ICmpInst::ICMP_EQ:
            cmp = (*c0 == *c1);
            break;
        case llvm::ICmpInst::ICMP_NE:
            cmp = (*c0 != *c1);
            break;
        case llvm::ICmpInst::ICMP_UGT:
            cmp = z3::ugt(*c0, *c1);
            break;
        case llvm::ICmpInst::ICMP_UGE:
            cmp = z3::uge(*c0, *c1);
            break;
        case llvm::ICmpInst::ICMP_ULT:
            cmp = z3::ult(*c0, *c1);
            break;
        case llvm::ICmpInst::ICMP_ULE:
            cmp = z3::ule(*c0, *c1);
            break;
        case llvm::ICmpInst::ICMP_SGT:
            cmp = (*c0 > *c1);
            break;
        case llvm::ICmpInst::ICMP_SGE:
            cmp = (*c0 >= *c1);
            break;
        case llvm::ICmpInst::ICMP_SLT:
            cmp = (*c0 < *c1);
            break;
        case llvm::ICmpInst::ICMP_SLE:
            cmp = (*c0 <= *c1);
            break;
        default:
            // If we encounter an unsupported predicate, we return a default formula (true) that does not constrain the analysis. In practice, you
            cmp = manager->Context->bool_val(true);
            break;
    }

    // If we are on the false branch of the constraint, we negate the formula to represent the negation of the constraint.
    if (!areWeInTheTrueBranch) {
        cmp = !cmp;
    }

    return cmp;
}

} // namespace Feasibility::Util
