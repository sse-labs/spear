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
    static void dumpFact(Feasibility::FeasibilityAnalysis *analysis, FeasibilityDomain::d_t fact);

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
     * Z3 helper functions to create formulas from LLVM values, e.g., to create a formula representing the value
     * of a variable at a certain program point. These are used in the edge functions to create the constraints that
     * we add to the path condition.
     */

    /**
     * Create a fresh bitvector variable for a given LLVM value. This is used to represent the value of a variable at
     * a certain program point, when we do not have a concrete value for it
     * (e.g., because it is derived from a load instruction).
     * @param key
     * @param bitwidth
     * @param prefix
     * @param context
     * @return
     */
    static z3::expr createFreshBitVal(const llvm::Value *key, unsigned bitwidth, const char *prefix, z3::context *context);

    /**
     * Create a z3 expression representing the value of a given LLVM value. This is used to create constraints from LLVM values.
     * If the value is a constant, we create a formula representing the constant value. If the value is not a constant, we create a fresh variable for it.
     * @param V LLVM value to create a formula for
     * @param context Z3 context to create the formula in
     * @return An optional z3 expression representing the value of the given LLVM value. The optional is empty if we cannot create a formula for the given value (e.g., if it is of an unsupported type).
     */
    static std::optional<z3::expr> createBitVal(const llvm::Value *V, z3::context *context);

    /**
     * Create a z3 expression representing the value of a given LLVM value, if it is an integer constant. This is used to create constraints from LLVM values.
     * If the value is a constant integer, we create a formula representing the constant value. If the value is not a constant integer, we return an empty optional.
     * @param val LLVM value to create a formula for
     * @param context Z3 context to create the formula in
     * @return An optional z3 expression representing the value of the given LLVM value if it is an integer constant, or an empty optional otherwise.
     */
    static std::optional<z3::expr> createIntVal(const llvm::Value *val, z3::context *context);

    /**
     * Create a z3 expression representing the value of a given LLVM value, if it is a pointer constant. This is used to create constraints from LLVM values.
     * If the value is a constant pointer, we create a formula representing the constant value. If the value is not a constant pointer, we return an empty optional.
     * @param val LLVM value to create a formula for
     * @param context Z3 context to create the formula in
     * @return An optional z3 expression representing the value of the given LLVM value if it is a constant pointer, or an empty optional otherwise.
     */
    static z3::expr mkSymBV(const llvm::Value *val, unsigned bitwidth, const char *prefix, z3::context *Ctx);

    /**
     * Find the given formula in the manager and return its ID, or create a new entry for it if it does not exist yet.
     * This is used to manage the formulas that we create from LLVM values and constraints, and to avoid creating
     * duplicate formulas for the same constraint.
     * @param manager
     * @param formula
     * @return
     */
    static uint32_t findOrAddFormulaId(FeasibilityAnalysisManager *manager, z3::expr formula);

    /**
     * Create a new z3 expression from the given ICmp instruction,
     * representing the constraint that the ICmp instruction imposes on the path condition.
     * This is used in the edge functions to create the constraints that we add to the path condition
     * when we encounter a branch instruction with an ICmp condition.
     * @param ICmp
     * @param areWeInTheTrueBranch
     * @return
     */
    static z3::expr createConstraintFromICmp(FeasibilityAnalysisManager *manager, const llvm::ICmpInst* ICmp, bool areWeInTheTrueBranch, uint32_t envId);

    /**
     * Check if the given block starts with a phinode
     * @param block
     * @return
     */
    static bool blockStartsWithPhi(const llvm::BasicBlock *block);


    static bool isTrueId(FeasibilityAnalysisManager *M, uint32_t id);

    static bool isFalseId(FeasibilityAnalysisManager *M, uint32_t id);

    static FeasibilityClause clauseFromIcmp(const llvm::ICmpInst *I, bool TrueEdge);

    static FeasibilityClause clauseFromPhi(const llvm::BasicBlock *Pred, const llvm::BasicBlock *Succ);

    static uint32_t applyPhiChain(FeasibilityAnalysisManager *M, uint32_t envId, const llvm::SmallVectorImpl<PhiStep> &chain);

    static void uniqAppend(llvm::SmallVector<uint32_t, 4> &out, uint32_t id);

    static void appendPhi(FeasibilityClause &C, const PhiStep &P);

    static void prependPhi(FeasibilityClause &C, const PhiStep &P);

    static FeasibilityClause conjClauses(const FeasibilityClause &A, const FeasibilityClause &B);

    static bool isRealPred(const llvm::BasicBlock *Pred, const llvm::BasicBlock *Succ);
};

}

#endif //SPEAR_UTIL_H