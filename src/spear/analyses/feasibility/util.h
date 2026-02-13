/*
 * Copyright (c) 2026 Maximilian Krebs
 * All rights reserved.
 */

#ifndef SPEAR_UTIL_H
#define SPEAR_UTIL_H
#include <atomic>

#include "FeasibilityAnalysis.h"

namespace Feasibility::Util {

extern std::atomic<bool> F_DebugEnabled;
inline constexpr const char *F_TAG = "[FDBG]";

const llvm::Value *stripAddr(const llvm::Value *pointer);

const llvm::Value *asValue(FeasibilityDomain::d_t fact);

/**
 * Dump fact of the current analysis
 *
 * @param analysis LoopBoundIDEAnalysis to dump from
 * @param fact Fact to dump
 */
void dumpFact(Feasibility::FeasibilityAnalysis *analysis, FeasibilityDomain::d_t fact);

/**
 * Dump Instruction
 * @param inst Instruction to dump
 */
void dumpInst(FeasibilityDomain::n_t inst);

void dumpEF(const Feasibility::FeasibilityAnalysis::EdgeFunctionType &edgeFunction);

void dumpEFKind(const Feasibility::FeasibilityAnalysis::EdgeFunctionType &E);

// Resolve an LLVM value into a Z3 expression using only the shared store/context.
// This creates symbolic variables for unknowns and models simple integer expressions.
std::optional<z3::expr> resolve(const llvm::Value *variable, FeasibilityStateStore *store);

z3::expr createFreshBitVal(const llvm::Value *key, unsigned bitwidth, const char *prefix, z3::context *context);

std::optional<z3::expr> createBitVal(const llvm::Value *V, z3::context *context);

std::optional<z3::expr> createIntVal(const llvm::Value *val, z3::context *context);

static z3::expr mkSymBV(const llvm::Value *V, unsigned BW, const char *prefix, z3::context *Ctx);

std::string stableName(const llvm::Value *V);

}

#endif //SPEAR_UTIL_H