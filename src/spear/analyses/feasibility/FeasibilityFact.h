/*
 * Copyright (c) 2026 Maximilian Krebs
 * All rights reserved.
 */

#ifndef SRC_SPEAR_ANALYSES_FEASIBILITY_FEASIBILITYFACT_H_
#define SRC_SPEAR_ANALYSES_FEASIBILITY_FEASIBILITYFACT_H_

#include <z3++.h>
#include <memory>
#include <utility>


namespace Feasibility {

class FeasibilityFact {
 public:
    /**
     * Solver context to create expressions in.
     */
    std::shared_ptr<z3::context> solverContext;

    /**
     * The path condition represented as a z3 expression.
     */
    z3::expr pathExpression;

    /**
     * Generic constructor to create a FeasibilityFact with the given context and path condition.
     * @param context
     * @param expression
     */
    FeasibilityFact(std::shared_ptr<z3::context> context, const z3::expr &expression)
    : solverContext(std::move(context)), pathExpression(expression) {}

    /**
     * Copy constructor
     * @param Other Other FeasibilityFact to copy from
     */
    FeasibilityFact(const FeasibilityFact &Other)
    : solverContext(Other.solverContext), pathExpression(Other.pathExpression) {}

    /**
     * Create a new true expression in the context of this fact.
     * @param context Context to create the true expression in
     * @return true expression
     */
    static FeasibilityFact TrueExpression(std::shared_ptr<z3::context> context);

    /**
     * Create a new false expression in the context of this fact.
     * @param context Context to create the false expression in
     * @return false expression
     */
    static FeasibilityFact FalseExpression(std::shared_ptr<z3::context> context);

    /**
     * Add the given constraint to the path condition of this fact and return a new fact with the updated path condition.
     * @param constraint Constraint to add
     * @return New FeasibilityFact with the updated path condition
     */
    FeasibilityFact addExpression(const z3::expr &constraint) const;

    /**
     * Compare this fact with another fact for ordering.
     * @param other Other fact to compare with
     * @return ordering result: true if this fact is less than the other fact, false otherwise
     */
    bool operator<(const FeasibilityFact &other) const noexcept;

    /**
     * Compare this fact with another fact for ordering.
     * @param other Other fact to compare with
     * @return ordering result: true if this fact is equal to the other fact, false otherwise
     */
    bool operator==(const FeasibilityFact &other) const noexcept;

    /**
     * Check if the path condition of this fact is satisfiable.
     * @return true if the path condition is satisfiable, false otherwise
     */
    bool isFeasible();
};

}  // namespace Feasibility

#endif  // SRC_SPEAR_ANALYSES_FEASIBILITY_FEASIBILITYFACT_H_
