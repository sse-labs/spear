/*
 * Copyright (c) 2026 Maximilian Krebs
 * All rights reserved.
 */

#include "analyses/feasibility/FeasibilityAnalysis.h"

namespace Feasibility::Util {


std::atomic<bool> F_DebugEnabled{false};

void dumpFact(Feasibility::FeasibilityAnalysis *analysis, Feasibility::FeasibilityDomain::d_t fact) {

}

void dumpInst(Feasibility::FeasibilityDomain::n_t instruction) {

}

void dumpEF(const Feasibility::FeasibilityAnalysis::EdgeFunctionType &edgeFunction) {};

}