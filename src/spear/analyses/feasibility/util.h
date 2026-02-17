/*
 * Copyright (c) 2026 Maximilian Krebs
 * All rights reserved.
 */

#ifndef SPEAR_UTIL_H
#define SPEAR_UTIL_H
#include <atomic>

#include "FeasibilityAnalysis.h"

#define F_TAG "[FDBG]"
#define F_DEBUG_ENABLED false

namespace Feasibility {

class Util {
 public:
    static const llvm::Value *stripAddr(const llvm::Value *pointer);

    static const llvm::Value *asValue(FeasibilityDomain::d_t fact);

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

    static void dumpEF(const Feasibility::FeasibilityAnalysis::EdgeFunctionType &edgeFunction);

    static void dumpEFKind(const Feasibility::FeasibilityAnalysis::EdgeFunctionType &E);

    // Resolve an LLVM value into a Z3 expression using only the shared store/context.
    // This creates symbolic variables for unknowns and models simple integer expressions.
    static std::optional<z3::expr> resolve(const llvm::Value *V, const FeasibilityElement &St, FeasibilityStateStore *Store);

    static z3::expr createFreshBitVal(const llvm::Value *key, unsigned bitwidth, const char *prefix, z3::context *context);

    static std::optional<z3::expr> createBitVal(const llvm::Value *V, z3::context *context);

    static std::optional<z3::expr> createIntVal(const llvm::Value *val, z3::context *context);

    static z3::expr mkSymBV(const llvm::Value *V, unsigned BW, const char *prefix, z3::context *Ctx);

    static std::string stableName(const llvm::Value *V);

    static const llvm::Instruction* firstRealInst(const llvm::BasicBlock *BB);

    /**
     * Report analysis metrics to stderr
     * @param Store FeasibilityStateStore containing metrics
     */
    static void reportMetrics(FeasibilityStateStore *Store);

    static std::optional<FeasibilityStateStore::ExprId> resolveId(const llvm::Value *V, const FeasibilityElement &St, FeasibilityStateStore *S);
};

}

#endif //SPEAR_UTIL_H