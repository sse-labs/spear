/*
 * Copyright (c) 2026 Maximilian Krebs
 * All rights reserved.
 */

#ifndef SPEAR_UTIL_H
#define SPEAR_UTIL_H

#include <atomic>

#include "FeasibilityAnalysis.h"
#include "FeasibilityEdgeFunction.h"

#define F_TAG "[FDBG]"
#define F_DEBUG_ENABLED false

namespace Feasibility {

using l_t = FeasibilityElement;
using EF  = psr::EdgeFunction<l_t>;

class Util {
public:

    /**
     * DEBUG HELPER: Dump the current state of the analysis
     */

    /**
     * Dump fact of the current analysis
     *
     * @param analysis LoopBoundIDEAnalysis to dump from
     * @param fact Fact to dump
     */
    static void dumpFact(Feasibility::FeasibilityAnalysis *analysis,
                         FeasibilityDomain::d_t fact);

    /**
     * Dump Instruction
     * @param inst Instruction to dump
     */
    static void dumpInst(FeasibilityDomain::n_t inst);

    /**
     * Dump edge function
     * @param edgeFunction
     */
    static void dumpEF(const Feasibility::FeasibilityAnalysis::EdgeFunctionType &edgeFunction);

    /**
     * Dump edge function kind
     * @param E Edge function to dump
     */
    static void dumpEFKind(const Feasibility::FeasibilityAnalysis::EdgeFunctionType &E);

    /*
     * Z3 helper functions to create formulas from LLVM values, e.g., to create a formula representing
     * the value of a variable at a certain program point. These are used in the edge functions to
     * create the constraints that we add to the path condition.
     */

    /**
     * Create a fresh bitvector variable for a given LLVM value. This is used to represent the value
     * of a variable at a certain program point when we do not have a concrete value for it
     * (e.g., because it is derived from a load instruction).
     */
    static z3::expr createFreshBitVal(const llvm::Value *key,
                                      unsigned bitwidth,
                                      const char *prefix,
                                      z3::context *context);

    /**
     * Create a z3 expression representing the value of a given LLVM value.
     * If the value is a constant, we create a formula representing the constant value.
     * If the value is not a constant, we create a fresh variable for it.
     */
    static std::optional<z3::expr> createBitVal(const llvm::Value *V,
                                               z3::context *context);

    /**
     * Create a z3 expression representing the value of a given LLVM value if it is an integer
     * constant.
     */
    static std::optional<z3::expr> createIntVal(const llvm::Value *val,
                                               z3::context *context);

    /**
     * Create a symbolic bitvector for the given LLVM value.
     */
    static z3::expr mkSymBV(const llvm::Value *val,
                            unsigned bitwidth,
                            const char *prefix,
                            z3::context *Ctx);

    /**
     * Create a new z3 expression from the given ICmp instruction representing the constraint
     * imposed on the path condition.
     */
    static z3::expr createConstraintFromICmp(FeasibilityAnalysisManager *manager,
                                             const llvm::ICmpInst *ICmp,
                                             bool areWeInTheTrueBranch,
                                             uint32_t envId);

    /**
     * Check if the given block starts with a PHI node.
     */
    static bool blockStartsWithPhi(const llvm::BasicBlock *block);


    static bool isRealPred(const llvm::BasicBlock *Pred,
                           const llvm::BasicBlock *Succ);

    static bool setSat(std::vector<z3::expr> set, z3::context *ctx);

    static bool isIdEF(const EF &ef) noexcept;

    static bool isAllTopEF(const EF &ef) noexcept;

    static bool isAllBottomEF(const EF &ef) noexcept;

    static FeasibilityAnalysisManager *pickManager(FeasibilityAnalysisManager *M, const l_t &source);
};

} // namespace Feasibility

#endif // SPEAR_UTIL_H