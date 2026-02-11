/*
 * Copyright (c) 2026 Maximilian Krebs
 * All rights reserved.
 */

#include "analyses/feasibility/FeasibilityFact.h"

namespace Feasibility {

FeasibilityFact FeasibilityFact::TrueExpression(std::shared_ptr<z3::context> context) {
    return FeasibilityFact(context, context->bool_val(true));
}

FeasibilityFact FeasibilityFact::FalseExpression(std::shared_ptr<z3::context> context) {
    return FeasibilityFact(context, context->bool_val(false));
}

FeasibilityFact FeasibilityFact::addExpression(const z3::expr &constraint) const {
    z3::expr newPathCondition = pathExpression && constraint;
    return FeasibilityFact(solverContext, newPathCondition);
}

bool FeasibilityFact::operator<(const FeasibilityFact &other) const noexcept {
    // Compare the path expressions as strings for ordering
    return pathExpression.to_string() < other.pathExpression.to_string();
}

bool FeasibilityFact::operator==(const FeasibilityFact &other) const noexcept {
    // Compare the path expressions as strings for ordering
    return pathExpression.to_string() == other.pathExpression.to_string();
}

bool FeasibilityFact::isFeasible() {
    z3::solver solverComponent(*this->solverContext);
    solverComponent.add(this->pathExpression);
    return solverComponent.check() == z3::sat;
}

}  // namespace Feasibility
