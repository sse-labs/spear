/*
 * Copyright (c) 2026 Maximilian Krebs
 * All rights reserved.
 */

#ifndef SPEAR_UTIL_H
#define SPEAR_UTIL_H

#include "FeasibilityAnalysis.h"

#define F_TAG "[FDBG]"

namespace Feasibility {

using l_t = FeasibilityElement;
using EF  = psr::EdgeFunction<l_t>;

/**
 * Utility class for the feasibility analysis,
 * providing helper functions for dumping facts, instructions, edge functions,
 * and creating z3 expressions from LLVM values.
 *
*/
class Util {
public:
    static constexpr std::string debugtag = "[FDBG] ";
    static constexpr std::atomic<bool> F_DebugEnabled{false};

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

    /**
     * Create a z3 expression representing the value of a given LLVM value.
     * If the value is a constant, we create a formula representing the constant value.
     * If the value is not a constant, we create a fresh variable for it.
     */
    static std::optional<z3::expr> createBitVal(const llvm::Value *val, z3::context *context);

    /**
     * Create a z3 expression representing the value of a given LLVM value if it is an integer
     * constant.
     * @param val The LLVM value for which to create the z3 expression.
     * @param context The z3 context to use for creating the expression.
     * @return A z3 expression representing the integer constant value of the given LLVM value,
     * or std::nullopt if the value is not an integer constant.
     */
    static std::optional<z3::expr> createIntVal(const llvm::Value *val, z3::context *context);

    /**
     * Create a symbolic bitvector for the given LLVM value.
     * @param val The LLVM value for which to create the symbolic bitvector.
     * @param bitwidth The bitwidth of the symbolic bitvector to create.
     * @param prefix The prefix to use for the name of the symbolic bitvector variable.
     * @param Ctx The z3 context to use for creating the symbolic bitvector.
     * @return A z3 expression representing the symbolic bitvector for the given LLVM value.
     */
    static z3::expr mkSymBV(const llvm::Value *val, unsigned bitwidth, const char *prefix, z3::context *Ctx);

    /**
     * Create a new z3 expression from the given ICmp instruction representing the constraint
     * imposed on the path condition.
     * @param manager The manager of the analysis, used to access the current environment and variable bindings.
     * @param ICmp The ICmp instruction for which to create the constraint.
     * @param areWeInTheTrueBranch A boolean indicating whether we are in the true
     * branch of the ICmp instruction (i.e., the branch where the condition holds) or in the false branch
     * @param envId The ID of the environment to use for looking up variable bindings in the manager.
     * @return A z3 expression representing the constraint imposed by the ICmp instruction on the
     */
    static z3::expr createConstraintFromICmp(FeasibilityAnalysisManager *manager,
                                             const llvm::ICmpInst *ICmp,
                                             bool areWeInTheTrueBranch,
                                             uint32_t envId);

    /**
     * Check if a set of z3 expressions is satisfiable.
     * @param set Set to check for satisfiability
     * @param ctx Context to use for checking satisfiability
     * @return true if the set is sat false otherwise
     */
    static bool setSat(std::vector<z3::expr> set, z3::context *ctx);

    /**
     * Check if the given edge function is an identity function
     * @param ef Edge function to check
     * @return true if the edge function is an identity function, false otherwise
     */
    static bool isIdEF(const EF &ef) noexcept;

    /**
     * Check if the given edge function is an all top function
     * @param ef Edge function to check
     * @return true if the edge function is an all top function, false otherwise
     */
    static bool isAllTopEF(const EF &ef) noexcept;

    /**
     * Check if the given edge function is an all bottom function
     * @param ef Edge function to check
     * @return true if the edge function is an all bottom function, false otherwise
     */
    static bool isAllBottomEF(const EF &ef) noexcept;

    /**
     * Choose the appropriate manager for the given source node.
     * @param manager First manager candidate
     * @param source Lattice element that provides another manager alternative
     * @return A valid manager to use
     */
    static FeasibilityAnalysisManager *pickManager(FeasibilityAnalysisManager *manager, const l_t &source);
};

} // namespace Feasibility

#endif // SPEAR_UTIL_H
