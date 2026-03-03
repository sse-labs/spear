/*
 * Copyright (c) 2026 Maximilian Krebs
 * All rights reserved.
 */

#ifndef SRC_SPEAR_ANALYSES_LOOPBOUND_CHECKEXPR_H_
#define SRC_SPEAR_ANALYSES_LOOPBOUND_CHECKEXPR_H_

#include <llvm/Analysis/LoopInfo.h>
#include <llvm/IR/Instructions.h>
#include <llvm/IR/PassManager.h>
#include <llvm/IR/Value.h>

#include <utility>
#include <unordered_map>
#include <map>
#include <string>

namespace LoopBound {
/**
 * Check Expression Class
 * Models checks, loops run against.
 * Allows for calculation of check values if they are no direct constant.
 *
 * We model loop checks as affine expression
 * check = offset + i * scaling
*/
class CheckExpr {
 public:
    /**
     * Base Value the expression is based on.
     * Should be a memory root derived from a load instruction
    */
    const llvm::Value *Base = nullptr;

    /**
     * Corresponding load instruction of the vase value
     */
    const llvm::LoadInst *BaseLoad = nullptr;

    /**
     * Represents the offset of our calculation
     */
    int64_t Offset = 0;

    /**
     * Flag to distinguish loaded checks from constants
     */
    bool isConstant = false;

    /**
     * Division value to represent the scaling factor 1/scaling
     */
    std::optional<int64_t> DivBy;

    /**
     * Multiplication value to represent the scaling factor scaling
     */
    std::optional<int64_t> MulBy;

    /**
     * Constructor
     * @param base Memory root the check is based on
     * @param baseload Load of the memory root
     * @param offset Constant offset
     */
    CheckExpr(const llvm::Value *base, const llvm::LoadInst *baseload, int64_t offset, bool constant) : Base(base),
        BaseLoad(baseload), Offset(offset), isConstant(constant) {
    }

    /**
     * Calculate the actual check value from the stored information
     * @param FAM FunctionAnalysisManger used to deduce constnats
     * @param LIInfo LoopInfo to infer loop related constants
     * @return Possible calculated check value
     */
    std::optional<int64_t> calculateCheck(llvm::FunctionAnalysisManager *FAM, llvm::LoopInfo &LIInfo);
};
}  // namespace LoopBound

#endif  // SRC_SPEAR_ANALYSES_LOOPBOUND_CHECKEXPR_H_
