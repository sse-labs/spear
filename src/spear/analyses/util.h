// /*
//  * Copyright (c) 2026 Maximilian Krebs
//  * All rights reserved.
// *

//
// Created by max on 1/28/26.
//

#ifndef SPEAR_UTIL_H
#define SPEAR_UTIL_H
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
    if (!LB_DebugEnabled.load()) return;
    if (!F) { llvm::errs() << "<null>"; return; }
    if (A->isZeroValue(F)) { llvm::errs() << "<ZERO>"; return; }
    const llvm::Value *V = asValue(F);
    const llvm::Value *S = A->stripAddr(V);
    llvm::errs() << V;
    if (S != V) {
        llvm::errs() << " (strip=" << S << ")";
    }
}

static inline void dumpInst(loopbound::LoopBoundIDEAnalysis::n_t I) {
    if (!LB_DebugEnabled.load()) return;
    if (!I) { llvm::errs() << "<null-inst>"; return; }
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

}

#endif //SPEAR_UTIL_H