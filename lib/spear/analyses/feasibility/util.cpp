/*
 * Copyright (c) 2026 Maximilian Krebs
 * All rights reserved.
 */

#include <llvm/IR/Operator.h>
#include <llvm/IR/Instructions.h>
#include <llvm/IR/Constants.h>
#include <llvm/IR/InstrTypes.h>
#include <llvm/IR/Type.h>

#include <string>

#include "analyses/feasibility/FeasibilityAnalysis.h"
#include "analyses/feasibility/FeasibilityAnalysisManager.h"
#include "analyses/feasibility/FeasibilityEdgeFunction.h"

namespace Feasibility {

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

void Util::dumpFact(Feasibility::FeasibilityAnalysis *analysis, Feasibility::FeasibilityDomain::d_t fact) {
    if (!F_DebugEnabled.load()) {
        return;
    }

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

void Util::dumpInst(Feasibility::FeasibilityDomain::n_t instruction) {
    if (!F_DebugEnabled.load()) {
        return;
    }

    if (!instruction) {
        llvm::errs() << "<null-inst>";
        return;
    }

    llvm::errs() << *instruction;
}

void Util::dumpEF(const Feasibility::FeasibilityAnalysis::EdgeFunctionType &edgeFunction) {
    if (edgeFunction.template isa<Feasibility::FeasibilityAllBottomEF>() ||
        llvm::isa<psr::AllBottom<Feasibility::l_t>>(edgeFunction)) {
        llvm::errs() << "EF=BOT";
        return;
    }

    llvm::errs() << "EF=<other>";
}

void Util::dumpEFKind(const EF &E) {
    llvm::errs() << "[EFKind=";

    if (E.template isa<FeasibilityAllBottomEF>()) {
        llvm::errs() << "FeasibilityAllBottomEF";
    } else if (llvm::isa<psr::AllTop<FeasibilityAnalysis::l_t>>(E)) {
        llvm::errs() << "psr::AllTop";
    } else if (llvm::isa<psr::AllBottom<FeasibilityAnalysis::l_t>>(E)) {
        llvm::errs() << "psr::AllBottom";
    } else if (llvm::isa<psr::EdgeIdentity<FeasibilityAnalysis::l_t>>(E)) {
        llvm::errs() << "psr::EdgeIdentity";
    } else {
        llvm::errs() << "UNKNOWN";
    }

    llvm::errs() << "]";
}

z3::expr Util::mkSymBV(const llvm::Value *V, unsigned BW, const char *prefix, z3::context *Ctx) {
    std::string name;

    if (V && V->hasName()) {
        name = std::string(prefix) + "_" + V->getName().str();
    } else {
        name = std::string(prefix) + "_" + std::to_string(reinterpret_cast<uintptr_t>(V));
    }

    return Ctx->bv_const(name.c_str(), BW);
}

std::optional<z3::expr> Util::createIntVal(const llvm::Value *val, z3::context *context) {
    if (auto constval = llvm::dyn_cast<llvm::ConstantInt>(val)) {
        unsigned bitwidth = constval->getBitWidth();
        uint64_t numval = constval->getValue().getZExtValue();
        return context->bv_val(numval, bitwidth);
    }

    return std::nullopt;
}

std::optional<z3::expr> Util::createBitVal(const llvm::Value *val, z3::context *context) {
    if (!val) {
        return std::nullopt;
    }

    if (auto C = createIntVal(val, context)) {
        return C;
    }

    if (val->getType()->isIntegerTy()) {
        unsigned bw = llvm::cast<llvm::IntegerType>(val->getType())->getBitWidth();
        return mkSymBV(val, bw, "v", context);
    }

    return std::nullopt;
}

z3::expr Util::createConstraintFromICmp(FeasibilityAnalysisManager *manager,
                                        const llvm::ICmpInst* ICmp,
                                        bool areWeInTheTrueBranch,
                                        uint32_t envId) {
    if (!manager) {
        llvm::errs() << "ALARM: createConstraintFromICmp called with null manager\n";
        // return a safe default
        static z3::context dummy;
        return dummy.bool_val(true);
    }

    if (!manager->hasEnv(envId)) {
        llvm::errs() << "ALARM: envId " << envId << " does not exist in manager. "
                     << "ICmp=" << *ICmp << "\n";
        // safest behavior: do not constrain instead of crashing
        return manager->getContext().bool_val(true);
    }

    auto op0 = manager->resolve(envId, ICmp->getOperand(0));
    auto op1 = manager->resolve(envId, ICmp->getOperand(1));

    auto c0 = createBitVal(op0, &manager->getContext());
    auto c1 = createBitVal(op1, &manager->getContext());

    if (!c0 || !c1) {
        // If we cannot create a formula for one of the operands,
        // we return a default formula (true) that does not constrain the analysis.
        llvm::errs() << "WARNING: Could not create constraint from ICmp instruction " << *ICmp
        << " because we could not create formulas for its operands.\n";
        return manager->getContext().bool_val(true);
    }

    // Init the constraint formula to true, and then update it based on the predicate of the ICmp instruction.
    // We use the appropriate Z3 operators for each predicate to create the correct constraint formula.
    z3::expr cmp = manager->getContext().bool_val(true);
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
            // If we encounter an unsupported predicate, we return a default formula (true) that
            // does not constrain the analysis.
            cmp = manager->getContext().bool_val(true);
            break;
    }

    // If we are on the false branch of the constraint, we negate the formula to represent the
    // negation of the constraint.
    if (!areWeInTheTrueBranch) {
        cmp = !cmp;
    }

    return cmp;
}

bool Util::setSat(std::vector<z3::expr> set, z3::context *ctx) {
    if  (set.empty()) {
        // An empty set represents the formula "true", which is satisfiable.
        return true;
    }

    z3::solver solver(*ctx);
    for (const auto &atom : set) {
        solver.add(atom);
    }

    return solver.check() == z3::sat;
}


FeasibilityAnalysisManager *Util::pickManager(FeasibilityAnalysisManager *M, const l_t &source) {
    if (source.getManager()) {
        return source.getManager();
    }

    return M;
}

bool Util::isIdEF(const EF &ef) noexcept {
    return ef.template isa<psr::EdgeIdentity<l_t>>();
}

bool Util::isAllTopEF(const EF &ef) noexcept {
    return ef.template isa<psr::AllTop<l_t>>();
}

bool Util::isAllBottomEF(const EF &ef) noexcept {
    return ef.template isa<FeasibilityAllBottomEF>() || ef.template isa<psr::AllBottom<l_t>>();
}

}  // namespace Feasibility
