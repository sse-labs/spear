/*
 * Copyright (c) 2026 Maximilian Krebs
 * All rights reserved.
 */

#include "analyses/feasibility/FeasibilityEdgeFunction.h"
#include "analyses/feasibility/util.h"
#include <chrono>

namespace Feasibility {

/**
 * Calculate what will happen, if we currently have the element source (the incoming lattice value, here the current path condition)
 * and apply the edge function (add the constraint represented by ConstraintInst) to it.
 */
l_t FeasibilityAddConstrainEF::computeTarget(const l_t &source) const {
    auto *manager = this->manager;

    if (source.isBottom()) {
        return source; // Bottom is absorbing a o F = F
    }

    // Prepare the constraint to add, depending on whether we are in the true or false branch of the constraint instruction.
    auto incomingConstrain = source.formularID;
    auto existingConstrain = this->pathConditionId;

    // Construct the new formula by adding the constraint represented by ConstraintInst to the source formula.
    auto newFormulaId = manager->mkAnd(incomingConstrain, existingConstrain);

    //llvm::errs() << "Computing target for FeasibilityAddConstrainEF: \n"
    //                "Adding constraint with ID " << incomingConstrain << " to source formula with ID " << existingConstrain << ". New formula ID is " << newFormulaId << "\n";

    if (!manager->isSat(newFormulaId)) {
        auto out = source;
        out.kind = l_t::Kind::Bottom;
        out.formularID = source.bottomId;
        return out;
    }

    // Copy the incoming element and update it with the new formula ID. This is the result of applying the edge function to the source element.
    auto out = source;
    out.formularID = newFormulaId;

    if (out.kind == l_t::Kind::Top) {
        out.kind = l_t::Kind::Normal;
    }

    return out;
}

EF FeasibilityAddConstrainEF::compose(psr::EdgeFunctionRef<FeasibilityAddConstrainEF> thisFunc, const EF &otherFunc) {
    // Check the types of the edge functions and act accordingly

    // Edge identy just returns this function as it does not change the input and thus does not affect the constraints
    // that we have already added to the path condition.
    if (otherFunc.template isa<psr::EdgeIdentity<l_t>>()) {
        return EF(thisFunc);
    }

    // Top edge function acts as neutral element for composition, as it does not change the input and thus does not
    // affect the constraints that we have already added to the path condition.
    if (otherFunc.template isa<FeasibilityAllTopEF>()) {
        return EF(thisFunc);
    }

    // Bottom edge function acts as absorbing element for composition,
    // as it maps any input to bottom and thus makes the path infeasible,
    // regardless of the constraints that we have already added to the path condition.
    if (otherFunc.template isa<FeasibilityAllBottomEF>()) {
        return EF(std::in_place_type<FeasibilityAllBottomEF>);
    }

    if (auto *otherC = otherFunc.template dyn_cast<FeasibilityAddConstrainEF>()) {
        // Take both icmps, construct a new formular, create a new edge function with the new constraint and return it.
        // This allows us to combine the constraints of both edge functions into a single edge function,
        // Most importantly here: Simplify the formular to mitigate double exprexissons

        // Query the path condition ids of each edge function
        auto thisConstrainId = thisFunc->pathConditionId;
        auto otherConstrainId = otherC->pathConditionId;

        // query the actual formulas from the manager using the ids
        auto thisFormula = thisFunc->manager->getExpression(thisConstrainId);
        auto otherFormula = otherC->manager->getExpression(otherConstrainId);

        //llvm::errs() << "Calculating composition of two FeasibilityAddConstrainEFs: " << thisFormula.to_string() << " && " << otherFormula.to_string() << "\n";

        // Construct the new formula by taking the conjunction of the two formulas.
        // This is the result of applying both edge functions to the input element.
        // h(x) = f(g(x)) => h(x) = f(x AND otherFormula) => h(x) = x AND thisFormula AND otherFormula
        auto newFormulaId = thisFunc->manager->mkAnd(thisConstrainId, otherConstrainId);

        //llvm::errs() << "Calculating compose of two FeasibilityAddConstrainEFs: " << thisFormula.to_string() << " || " << otherFormula.to_string() << "\n";
        //llvm::errs() << "\t" << "Resulting formula: " << thisFunc->manager->getExpression(newFormulaId).to_string() << "\n";

        // We could use caching here...
        /*if (!thisFunc->manager->isSat(newFormulaId)) {
            thisFunc->manager->SatCache[newFormulaId] = FeasibilityAnalysisManager::SatTri::Unknown;
            //llvm::errs() << "\t" << "Resulting formula is UNSAT\n";
            return EF(std::in_place_type<FeasibilityAllBottomEF>);
        }*/

        // Return a new edge function that adds the new constraint to the path condition.
        // This is the result of composing the two edge functions.
        return EF(std::in_place_type<FeasibilityAddConstrainEF>, thisFunc->manager, newFormulaId);
    }

    llvm::errs() << "ALARM in FeasibilityAddConstrainEF::compose: We are trying to compose an edge function of type FeasibilityAddConstrainEF with an edge function of an unsupported type. This should not happen, as the analysis should only produce edge functions of the supported types. "
                    "Please check the implementation of the analysis and make sure that it only produces edge functions of the supported types.\n";
    return otherFunc;
}

EF FeasibilityAddConstrainEF::join(psr::EdgeFunctionRef<FeasibilityAddConstrainEF> thisFunc, const psr::EdgeFunction<l_t> &otherFunc) {
    // Edge identy just returns this function as it does not change the input and thus does not affect the constraints
    // that we have already added to the path condition.
    if (otherFunc.template isa<psr::EdgeIdentity<l_t>>()) {
        return EF(thisFunc);
    }

    // Top edge function acts as absorbing element for joining.
    // As we are calculaling A v T = T
    if (otherFunc.template isa<FeasibilityAllTopEF>()) {
        return EF(std::in_place_type<FeasibilityAllTopEF>);
    }

    // Bottom edge function acts as neutral element for joining,
    // as it maps any input to itself A V ⊥ = A
    if (otherFunc.template isa<FeasibilityAllBottomEF>()) {
        //llvm::errs() << "Joining FeasibilityAddConstrainEF with FeasibilityAllBottomEF. Result is the original FeasibilityAddConstrainEF, as A V ⊥ = A\n";
        return EF{thisFunc};
    }

    if (auto *otherC = otherFunc.template dyn_cast<FeasibilityAddConstrainEF>()) {
        // Take both formularids, construct a new formular, create a new edge function with the new constraint and return it.
        // This allows us to combine the constraints of both edge functions into a single edge function,
        // Most importantly here: Simplify the formular to mitigate double exprexissons

        // Query the path condition ids of each edge function
        auto thisConstrainId = thisFunc->pathConditionId;
        auto otherConstrainId = otherC->pathConditionId;

        // query the actual formulas from the manager using the ids
        auto thisFormula = thisFunc->manager->getExpression(thisConstrainId);
        auto otherFormula = otherC->manager->getExpression(otherConstrainId);

        // Construct the new formula by taking the disjunction of the two formulas.
        // This is the result of applying both edge functions to the input element.
        // h(x) = f(x) V g(x) => h(x) = (x AND thisFormula) V (x AND otherFormula) => h(x) = x AND (thisFormula V otherFormula)
        auto newFormulaId = thisFunc->manager->mkOr(thisConstrainId, otherConstrainId);

        //llvm::errs() << "Calculating join of two FeasibilityAddConstrainEFs: " << thisFormula.to_string() << " || " << otherFormula.to_string() << "=" << newFormulaId << "\n";
        //llvm::errs() << "\t" << "Resulting formula: " << thisFunc->manager->getExpression(newFormulaId).to_string() << "\n";

        /**/

        if (!thisFunc->manager->isSat(newFormulaId)) {
            thisFunc->manager->SatCache[newFormulaId] = FeasibilityAnalysisManager::SatTri::Unsat;
            //llvm::errs() << "\t" << "Resulting formula is UNSAT\n";
            return EF(std::in_place_type<FeasibilityAllBottomEF>);
        }

        //llvm::errs() << "Join " << "\n";
        //llvm::errs() << thisFunc->manager->getExpression(newFormulaId).to_string() << "\n";

        // Return a new edge function that adds the new constraint to the path condition.
        // This is the result of composing the two edge functions.
        return EF(std::in_place_type<FeasibilityAddConstrainEF>, thisFunc->manager, newFormulaId);
    }

    llvm::errs() << "ALARM in FeasibilityAddConstrainEF::join: We are trying to join an edge function of type FeasibilityAddConstrainEF with an edge function of an unsupported type. This should not happen, as the analysis should only produce edge functions of the supported types. "
                    "Please check the implementation of the analysis and make sure that it only produces edge functions of the supported types.\n";
    return otherFunc;
}

// =====================================================================================================================

l_t FeasibilityAllTopEF::computeTarget(const l_t &source) const {
    return l_t::createElement(source.manager, source.topId, l_t::Kind::Top);
}

EF FeasibilityAllTopEF::compose(psr::EdgeFunctionRef<FeasibilityAllTopEF> thisFunc, const EF &secondFunction) {
    if (secondFunction.template isa<psr::EdgeIdentity<l_t>>()) {
        return EF(std::in_place_type<FeasibilityAllTopEF>);
    }

    // Composition of T and ⊥ is ⊥, as T is the identity for composition and thus does not affect the result of the
    // composition, while ⊥ is absorbing for composition and thus makes the result of the composition ⊥.
    if (secondFunction.template isa<FeasibilityAllBottomEF>() || llvm::isa<psr::AllBottom<l_t>>(secondFunction)) {
        return EF(std::in_place_type<FeasibilityAllBottomEF>);
    }

    // Composition of T and T is T, as T is the identity for composition and thus does not affect the result of the composition.
    if (secondFunction.template isa<FeasibilityAllTopEF>() || llvm::isa<psr::AllTop<l_t>>(secondFunction)) {
        return EF(std::in_place_type<FeasibilityAllTopEF>);
    }

    // Composition of T and A is T
    if (secondFunction.template isa<FeasibilityAddConstrainEF>()) {
        return EF(secondFunction);
    }

    llvm::errs() << "ALARM in FeasibilityAllTopEF::compose: We are trying to compose an edge function of type FeasibilityAllTopEF with an edge function of an unsupported type. This should not happen, as the analysis should only produce edge functions of the supported types. "
                    "Please check the FeasibilityAllTopEF of the analysis and make sure that it only produces edge functions of the supported types.\n";

    return EF(std::in_place_type<FeasibilityAllTopEF>);
}

EF FeasibilityAllTopEF::join(psr::EdgeFunctionRef<FeasibilityAllTopEF> thisFunc, const psr::EdgeFunction<l_t> &secondFunction) {
    if (secondFunction.template isa<psr::EdgeIdentity<l_t>>()) {
        return EF(std::in_place_type<FeasibilityAllTopEF>);
    }

    // Composition of T or ⊥ is T, as T is the absorbing element for joining
    if (secondFunction.template isa<FeasibilityAllBottomEF>() || llvm::isa<psr::AllBottom<l_t>>(secondFunction)) {
        return EF(std::in_place_type<FeasibilityAllTopEF>);
    }

    // Joining of T and T is T, as T is the absorbing element for joining.
    if (secondFunction.template isa<FeasibilityAllTopEF>() || llvm::isa<psr::AllTop<l_t>>(secondFunction)) {
        return EF(std::in_place_type<FeasibilityAllTopEF>);
    }

    // Join of T and A is T
    if (secondFunction.template isa<FeasibilityAddConstrainEF>()) {
        return EF(std::in_place_type<FeasibilityAllTopEF>);
    }

    llvm::errs() << "ALARM in FeasibilityAllTopEF::join: We are trying to join an edge function of type FeasibilityAllTopEF with an edge function of an unsupported type. This should not happen, as the analysis should only produce edge functions of the supported types. "
                    "Please check the FeasibilityAllTopEF of the analysis and make sure that it only produces edge functions of the supported types.\n";

    return EF(std::in_place_type<FeasibilityAllTopEF>);
}

// =====================================================================================================================

l_t FeasibilityAllBottomEF::computeTarget(const l_t &source) const {
    return l_t::createElement(source.manager, source.bottomId, l_t::Kind::Bottom);
}

EF FeasibilityAllBottomEF::compose(psr::EdgeFunctionRef<FeasibilityAllBottomEF> thisFunc, const EF &secondFunction) {
    if (secondFunction.template isa<psr::EdgeIdentity<l_t>>()) {
        return EF(std::in_place_type<FeasibilityAllBottomEF>);
    }

    // Composition of ⊥ and ⊥ is ⊥, as ⊥ is the absorbing element for composition and thus does not affect the result of the
    if (secondFunction.template isa<FeasibilityAllBottomEF>() || llvm::isa<psr::AllBottom<l_t>>(secondFunction)) {
        return EF(std::in_place_type<FeasibilityAllBottomEF>);
    }

    // Composition of ⊥ and T is ⊥, as ⊥ absorbs T in composition, as ⊥ maps any input to bottom and thus makes the
    // path infeasible, regardless of the constraints that we have already added to the path condition.
    if (secondFunction.template isa<FeasibilityAllTopEF>() || llvm::isa<psr::AllTop<l_t>>(secondFunction)) {
        return EF(std::in_place_type<FeasibilityAllBottomEF>);
    }

    // Composition of ⊥ and A is A
    if (secondFunction.template isa<FeasibilityAddConstrainEF>()) {
        return EF(std::in_place_type<FeasibilityAllBottomEF>);
    }

    llvm::errs() << "ALARM in FeasibilityAllBottomEF::compose: We are trying to compose an edge function of type FeasibilityAllBottomEF with an edge function of an unsupported type. This should not happen, as the analysis should only produce edge functions of the supported types. "
                    "Please check the FeasibilityAllBottomEF of the analysis and make sure that it only produces edge functions of the supported types.\n";

    return EF(std::in_place_type<FeasibilityAllBottomEF>);
}

EF FeasibilityAllBottomEF::join(psr::EdgeFunctionRef<FeasibilityAllBottomEF> thisFunc, const psr::EdgeFunction<l_t> &secondFunction) {
    if (secondFunction.template isa<psr::EdgeIdentity<l_t>>()) {
        return EF(std::in_place_type<FeasibilityAllBottomEF>);
    }

    // Joining of ⊥ or ⊥ is ⊥, as ⊥ is the absorbing element for joining
    if (secondFunction.template isa<FeasibilityAllBottomEF>() || llvm::isa<psr::AllBottom<l_t>>(secondFunction)) {
        return EF(std::in_place_type<FeasibilityAllBottomEF>);
    }

    // Joining of ⊥ or T is T
    if (secondFunction.template isa<FeasibilityAllTopEF>() || llvm::isa<psr::AllTop<l_t>>(secondFunction)) {
        return EF(std::in_place_type<FeasibilityAllTopEF>);
    }

    // Joining of ⊥ or A is T
    if (secondFunction.template isa<FeasibilityAddConstrainEF>()) {
        return EF(secondFunction);
    }

    llvm::errs() << "ALARM in FeasibilityAllBottomEF::join: We are trying to join an edge function of type FeasibilityAllBottomEF with an edge function of an unsupported type. This should not happen, as the analysis should only produce edge functions of the supported types. "
                    "Please check the FeasibilityAllBottomEF of the analysis and make sure that it only produces edge functions of the supported types.\n";

    return EF(std::in_place_type<FeasibilityAllTopEF>);
}

} // namespace Feasibility
