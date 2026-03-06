
/*
 * Copyright (c) 2026 Maximilian Krebs
 * All rights reserved.
 */

#ifndef SPEAR_RESULTREGISTRY_H
#define SPEAR_RESULTREGISTRY_H
#include "feasibility/FeasibilityAnalysis.h"
#include "loopbound/LoopBound.h"

class ResultRegistry {
public:
    /**
     * Create a new empty ResultRegistry to store the results of the analyses.
     */
    explicit ResultRegistry();

    /**
     * Store the results of the feasibility analysis in the registry.
     * @param results Result to store
     */
    void storeFeasibilityResults(Feasibility::FunctionFeasibilityMap &results);

    /**
     * Store the results of the feasibility analysis in the registry.
     * @param results Result to store
     */
    void storeLoopBoundResults(LoopBound::LoopFunctionMap &results);

    /**
    * Get the results of the feasibility analysis from the registry.
    * @return Results of the feasibility analysis
    */
    Feasibility::FunctionFeasibilityMap getFeasibilityResults() const;

    /**
    * Get the results of the loop bound analysis from the registry.
    * @return Results of the loop bound analysis
    */
    LoopBound::LoopFunctionMap getLoopBoundResults() const;

    /**
    * Clear all stored results from the registry.
    */
    void clearResults();
private:
    /**
     * Results of the feasibility analysis, stored as a map from function names to their basic blocks and their
     * feasibility status.
     */
    Feasibility::FunctionFeasibilityMap feasibilityResults;

    /**
     * Results of the loop bound analysis, stored as a map from function names to their loops and their upper bounds.
     */
    LoopBound::LoopFunctionMap loopboundResults;
};

#endif  // SPEAR_RESULTREGISTRY_H
