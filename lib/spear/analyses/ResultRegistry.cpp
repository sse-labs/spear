/*
 * Copyright (c) 2026 Maximilian Krebs
 * All rights reserved.
 */

#include "analyses/ResultRegistry.h"


ResultRegistry::ResultRegistry() {
    /**
     * Nothing to do here...
     */
}

void ResultRegistry::storeFeasibilityResults(Feasibility::FunctionFeasibilityMap &results) {
    this->feasibilityResults = results;
}

void ResultRegistry::storeLoopBoundResults(LoopBound::LoopFunctionMap &results) {
    this->loopboundResults = results;
}

Feasibility::FunctionFeasibilityMap ResultRegistry::getFeasibilityResults() const {
    return this->feasibilityResults;
}

LoopBound::LoopFunctionMap ResultRegistry::getLoopBoundResults() const {
    return this->loopboundResults;
}

void ResultRegistry::clearResults() {
    this->feasibilityResults.clear();
    this->loopboundResults.clear();
}
