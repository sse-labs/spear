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

}

#endif //SPEAR_UTIL_H