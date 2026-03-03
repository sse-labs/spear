/*
 * Copyright (c) 2026 Maximilian Krebs
 * All rights reserved.
 */
#include <llvm/IR/Dominators.h>

#include "analyses/loopbound/CheckExpr.h"
#include "analyses/loopbound/util.h"

std::optional<int64_t> LoopBound::CheckExpr::calculateCheck(
    llvm::FunctionAnalysisManager *analysisManager, llvm::LoopInfo &loopInfo) {
    (void)analysisManager;  // may be null / unconfigured don't depend on it here.

    if (!this->isConstant && this->BaseLoad) {
        const llvm::Function *currentFunction = this->BaseLoad->getFunction();
        if (currentFunction) {
            // Build dominator tree locally (no FAM).
            llvm::DominatorTree dominatorTree(*const_cast<llvm::Function *>(currentFunction));

            if (auto constValue =
                LoopBound::Util::tryDeduceConstFromLoad(this->BaseLoad, dominatorTree, loopInfo)) {
                auto combinedValue = *constValue + this->Offset;
                if (MulBy) return combinedValue * MulBy.value();
                if (DivBy) return combinedValue / DivBy.value();
                return combinedValue;
                }
        }
    }

    if (this->isConstant) {
        return this->Offset;
    }

    return std::nullopt;
}
