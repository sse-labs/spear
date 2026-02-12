/*
 * Copyright (c) 2026 Maximilian Krebs
 * All rights reserved.
 */

#include <llvm/IR/Operator.h>

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
        llvm::errs() << "EF=ASSUME[" << ssaset->ValueExpr.to_string() << "]";
        return;
    }

    if (auto *memsset = edgeFunction.template dyn_cast<Feasibility::FeasibilitySetMemEF>()) {
        llvm::errs() << "EF=ASSUME[" << memsset->ValueExpr.to_string() << "]";
        return;
    }

    llvm::errs() << "EF=<other>";
};

}
