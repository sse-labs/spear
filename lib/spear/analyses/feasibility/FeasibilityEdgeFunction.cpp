/*
 * Copyright (c) 2026 Maximilian Krebs
 * All rights reserved.
 */

#include "analyses/feasibility/FeasibilityEdgeFunction.h"
#include "analyses/feasibility/util.h"
#include <chrono>

namespace Feasibility {


// =====================================================================================================================
// FeasibilityAllTopEF

l_t FeasibilityAllTopEF::computeTarget(const l_t &source) const {
    // Preserve manager pointer; set (kind=Top, formula=topId)
    auto out = source;
    out.kind = l_t::Kind::Top;
    out.formularID = l_t::topId; // or source.topId if you store it there
    // env can stay unchanged
    return out;
}

EF FeasibilityAllTopEF::compose(psr::EdgeFunctionRef<FeasibilityAllTopEF> /*thisFunc*/,
                                const EF &/*secondFunction*/) {
  // AllTop ∘ g = AllTop   (maps everything to top regardless of g)
  return EF(std::in_place_type<FeasibilityAllTopEF>);
}

EF FeasibilityAllTopEF::join(psr::EdgeFunctionRef<FeasibilityAllTopEF> /*thisFunc*/,
                             const psr::EdgeFunction<l_t> &/*otherFunc*/) {
    // Compose = this ∘ second (apply second first, then this).
    // Since this maps EVERYTHING to Top, it dominates composition.
    // T ∘ g = T
    return EF(std::in_place_type<FeasibilityAllTopEF>);
}

// =====================================================================================================================
// FeasibilityAllBottomEF

l_t FeasibilityAllBottomEF::computeTarget(const l_t &source) const {
    auto out = source;
    out.kind = l_t::Kind::Bottom;
    out.formularID = l_t::bottomId; // or source.bottomId if you store it there
    // env can stay unchanged
    return out;
}

EF FeasibilityAllBottomEF::compose(psr::EdgeFunctionRef<FeasibilityAllBottomEF> /*thisFunc*/,
                                   const EF &/*secondFunction*/) {
    // AllBottom ∘ g = AllBottom (once infeasible, stay infeasible)
    return EF(std::in_place_type<FeasibilityAllBottomEF>);
}

EF FeasibilityAllBottomEF::join(psr::EdgeFunctionRef<FeasibilityAllBottomEF> /*thisFunc*/,
                                const psr::EdgeFunction<l_t> &otherFunc) {
    // Join is OR of results. Bottom is neutral:
    // ⊥ ⊔ f = f
    if (otherFunc.template isa<psr::EdgeIdentity<l_t>>()) {
        return EF(std::in_place_type<psr::EdgeIdentity<l_t>>);
    }
    if (otherFunc.template isa<FeasibilityAllTopEF>() || otherFunc.template isa<psr::AllTop<l_t>>()) {
        return EF(std::in_place_type<FeasibilityAllTopEF>);
    }
    if (otherFunc.template isa<FeasibilityAllBottomEF>() || otherFunc.template isa<psr::AllBottom<l_t>>()) {
        return EF(std::in_place_type<FeasibilityAllBottomEF>);
    }

    // For AND/OR formula EFs, AddConstrainEF, PhiTranslateEF, etc.:
    return EF(otherFunc);
}

// =====================================================================================================================
// FeasibilityPHITranslateEF

l_t FeasibilityPHITranslateEF::computeTarget(const l_t &source) const {
    auto *localManager = this->manager ? this->manager : source.manager;

    if (source.isBottom()) {
        return source;
    }

    if (!PredBB || !SuccBB) {
        return source; // behave like Identity
    }

    const uint32_t incomingEnvId = source.environmentID;

    const uint32_t outEnvId = localManager->applyPhiPack(incomingEnvId, PredBB, SuccBB);

    auto out = source;
    out.environmentID = outEnvId;

    return out;
}

EF FeasibilityPHITranslateEF::compose(psr::EdgeFunctionRef<FeasibilityPHITranslateEF> thisFunc, const EF &secondFunction) {

    // Identity case. Do nothing
    if (secondFunction.template isa<psr::EdgeIdentity<l_t>>()) {
        return EF(thisFunc);
    }

    // Bottom case. Phi ° ⊥ = ⊥
    if (secondFunction.template isa<FeasibilityAllBottomEF>()) {
        return EF(std::in_place_type<FeasibilityAllBottomEF>);
    }

    // Top case. Phi ° ⊤ = ⊤
    if (secondFunction.template isa<FeasibilityAllTopEF>()) {
        return EF(std::in_place_type<FeasibilityAllTopEF>);
    }

    // Any other case...

    // Our current phi translation step
    const auto step = PhiStep(thisFunc->PredBB, thisFunc->SuccBB);
    auto *M = thisFunc->manager;

    // Deals with two phis following after each other.
    // We can just concatenate the phi chains, as applying the two phis in sequence is the same as applying the concatenated chain.
    // Pray to your creator that this does not happen...
    if (auto *otherPhi = secondFunction.template dyn_cast<FeasibilityPHITranslateEF>()) {
        FeasibilityClause clause;

        // Push the other phi's step first, as it will be applied first when we apply the composed function to an element.
        clause.PhiChain.push_back(PhiStep(otherPhi->PredBB, otherPhi->SuccBB));
        clause.PhiChain.push_back(step);
        return EF(std::in_place_type<FeasibilityANDFormulaEF>, M, std::move(clause));
    }

    // If we encounter another constraint after the phi translation, we need to apply the phi translation first and
    // then add the constraint. This is because the constraint may refer to variables that are translated by the
    // phi translation, and we need to make sure that we apply the phi translation before adding the
    // constraint to ensure that we are adding the constraint to the correct variables in the environment.
    if (auto *addCons = secondFunction.template dyn_cast<FeasibilityAddConstrainEF>()) {
        FeasibilityClause clause;
        clause.PhiChain.push_back(step);
        clause.Constrs.push_back(LazyICmp(addCons->ConstraintInst, addCons->isTrueBranch));
        return EF(std::in_place_type<FeasibilityANDFormulaEF>, M, std::move(clause));
    }

    // Deal with ANDs and ORs

    // AND case. We need to prepend the current phi step to translate all other constraints in the clause.
    if (auto *andEF = secondFunction.template dyn_cast<FeasibilityANDFormulaEF>()) {
        FeasibilityClause clause = andEF->Clause;
        // Prepend our PHI translation step to the clause's PHI chain, as it needs to be applied before the constraints in the clause.
        clause.PhiChain.insert(clause.PhiChain.begin(), step);
        return EF(std::in_place_type<FeasibilityANDFormulaEF>, M, std::move(clause));
    }

    // OR case. We need to prepend the current phi step to all clauses in the OR, as it needs to be applied before the constraints in the clauses.
    if (auto *orEF = secondFunction.template dyn_cast<FeasibilityORFormulaEF>()) {
        auto existingClauses = orEF->Clauses;
        for (auto &clause : existingClauses) {
            clause.PhiChain.insert(clause.PhiChain.begin(), step);
        }
        return EF(std::in_place_type<FeasibilityORFormulaEF>, M, std::move(existingClauses));
    }

    llvm::errs() << "ALARM in FeasibilityPHITranslateEF::compose: We are trying to compose an edge function of type FeasibilityPHITranslateEF with an edge function of an unsupported type. This should not happen, as the analysis should only produce edge functions of the supported types. "
                    "Please check the implementation of the analysis and make sure that it only produces edge functions of the supported types.\n";
    return secondFunction;
}

EF FeasibilityPHITranslateEF::join(psr::EdgeFunctionRef<FeasibilityPHITranslateEF> thisFunc, const psr::EdgeFunction<l_t> &otherFunc) {

    // Identity case. Do nothing
    if (otherFunc.template isa<psr::EdgeIdentity<l_t>>()) {
        return EF(std::in_place_type<psr::EdgeIdentity<l_t>>);
    }

    // Top case. Phi V ⊤ = ⊤
    if (otherFunc.template isa<FeasibilityAllTopEF>()) {
        return EF(std::in_place_type<FeasibilityAllTopEF>);
    }

    // Bottom case. Phi V ⊥ = Phi
    if (otherFunc.template isa<FeasibilityAllBottomEF>()) {
        return EF(thisFunc);
    }

    // Any other case...
    auto *M = thisFunc->manager;

    // If we want to join with another function we need to represent this phi also as clause
    FeasibilityClause thisClause;
    thisClause.PhiChain.push_back(PhiStep(thisFunc->PredBB, thisFunc->SuccBB));

    // Phi case. Joining two phis with different translation steps results in an OR of the two phis, as we want to
    // represent that either of the two translations may be applied.
    if (auto *otherPhi = otherFunc.template dyn_cast<FeasibilityPHITranslateEF>()) {
        // Create a new clause for the other phi translation step, as we need to represent it as a disjunction
        // in the resulting OR formula.
        FeasibilityClause otherClause;
        otherClause.PhiChain.push_back(PhiStep(otherPhi->PredBB, otherPhi->SuccBB));

        // Create the disjunction of the feasibility clauses
        llvm::SmallVector<FeasibilityClause, 4> clauses;
        // Add the clause for this phi translation step
        clauses.push_back(std::move(thisClause));
        // Add the clause for the other phi translation step
        clauses.push_back(std::move(otherClause));

        // Create an or with the two clauses
        return EF(std::in_place_type<FeasibilityORFormulaEF>, M, clauses);
    }

    if (auto *addCons = otherFunc.template dyn_cast<FeasibilityAddConstrainEF>()) {
        // If we want to join with a constraint, we need to represent the phi translation as a clause and add the constraint to this clause,
        // as we want to represent that the constraint is only added if the phi translation is applied.

        // Create a new clause from the incoming add
        FeasibilityClause clauseofAdd = Util::clauseFromIcmp(addCons->ConstraintInst, addCons->isTrueBranch);

        // Create a vector of clauses we want to OR together, which consists of the clause for this phi translation
        // step and the clause for the other constraint.
        llvm::SmallVector<FeasibilityClause, 4> clauses;
        clauses.push_back(std::move(thisClause));
        clauses.push_back(std::move(clauseofAdd));

        return EF(std::in_place_type<FeasibilityORFormulaEF>, M, clauses);
    }

    // Deal with ANDs and ORs

    // AND case. We need to join the current phi step with the existing constraints in the clause,
    // as we want to represent that the phi translation is applied together with the constraints in the clause.
    if (auto *andEF = otherFunc.template dyn_cast<FeasibilityANDFormulaEF>()) {
        // Create a new vector of clauses where we want to represent this phi and the incoming AND clause as a
        // disjunction of the two, as we want to represent that either the phi translation is applied
        // or the constraints in the clause are applied.
        llvm::SmallVector<FeasibilityClause, 4> clauses;
        clauses.push_back(std::move(thisClause));
        clauses.push_back(andEF->Clause);

        return EF(std::in_place_type<FeasibilityORFormulaEF>, M, std::move(clauses));
    }

    // OR case. We need to join the current phi step with all existing clauses in the OR,
    // as we want to represent that the phi translation is applied together with all constraints in the clauses.
    if (auto *orEF = otherFunc.template dyn_cast<FeasibilityORFormulaEF>()) {
        // Get the clauses of the incoming oR
        auto clauses = orEF->Clauses;
        clauses.push_back(std::move(thisClause));
        return EF(std::in_place_type<FeasibilityORFormulaEF>, M, std::move(clauses));
    }

    llvm::errs() << "ALARM in FeasibilityPHITranslateEF::join: unsupported otherFunc\n";
    return EF(otherFunc);
}

// =====================================================================================================================
// FeasibilityAddConstrainEF

l_t FeasibilityAddConstrainEF::computeTarget(const l_t &source) const {
    auto *localManager = this->manager ? this->manager : source.manager;

    if (source.isBottom()) {
        return source;
    }

    // If the incoming ICMP is borked, we just behave as identity
    // This should not happen, as we should only create FeasibilityAddConstrainEF for valid ICMP instructions
    if (!ConstraintInst) {
        llvm::errs() << "ALARM in FeasibilityAddConstrainEF::computeTarget: ConstraintInst is null."
                        "This should not happen, as we should only create FeasibilityAddConstrainEF for valid ICMP "
                        "instructions. Please check the implementation of the analysis and make sure that we "
                        "are only creating FeasibilityAddConstrainEF for valid ICMP instructions.\n";
        return source;
    }

    const uint32_t incomingPathCondition = source.formularID;

    // Create a new constraint from the incoming ICMP instruction, the branch condition (true or false) and
    // the environment of the source element.
    z3::expr newConstraint = Util::createConstraintFromICmp(localManager, ConstraintInst, isTrueBranch, source.environmentID);
    const uint32_t constraintId = Util::findOrAddFormulaId(localManager, newConstraint);

    // Construct the new formula by adding the constraint represented by ConstraintInst to the source formula.
    auto outPathConditionId = localManager->mkAnd(incomingPathCondition, constraintId);

    //llvm::errs() << "Computing target for FeasibilityAddConstrainEF: \n"
    //                "Adding constraint with ID " << incomingConstrain << " to source formula with ID " << existingConstrain << ". New formula ID is " << newFormulaId << "\n";

    if (!localManager->isSat(outPathConditionId)) {
        auto out = source;
        out.kind = l_t::Kind::Bottom;
        out.formularID = source.bottomId;
        return out;
    }

    // Copy the incoming element and update it with the new formula ID. This is the result of applying the edge function to the source element.
    auto out = source;
    out.formularID = outPathConditionId;

    if (out.kind == l_t::Kind::Top) {
        out.kind = l_t::Kind::Normal;
    }

    return out;
}

EF FeasibilityAddConstrainEF::compose(psr::EdgeFunctionRef<FeasibilityAddConstrainEF> thisFunc, const EF &secondFunction) {
    // Check the types of the edge functions and act accordingly

    // Identity do nothing
    if (secondFunction.template isa<psr::EdgeIdentity<l_t>>()) {
        return EF(thisFunc);
    }

    // Bottom kills everything
    if (secondFunction.template isa<FeasibilityAllBottomEF>()) {
        return EF(std::in_place_type<FeasibilityAllBottomEF>);
    }

    // Top kills everything
    if (secondFunction.template isa<FeasibilityAllTopEF>()) {
        return EF(std::in_place_type<FeasibilityAllTopEF>);
    }

    auto *M = thisFunc->manager;

    // Create new clause for this constraint that we want to add,
    // as we need to represent it as a clause when we compose with other constraints or phis.
    FeasibilityClause thisClause;
    // Add the constraint represented by this edge function to the clause, as we need to represent it as a
    // clause when we compose with other constraints or phis.
    thisClause.Constrs.push_back(LazyICmp(thisFunc->ConstraintInst, thisFunc->isTrueBranch));

    // Compose with an incoming phi
    if (auto *otherPhi = secondFunction.template dyn_cast<FeasibilityPHITranslateEF>()) {
        FeasibilityClause otherClause = thisClause;

        // Do we need to insert at the beginning here?
        otherClause.PhiChain.push_back(PhiStep(otherPhi->PredBB, otherPhi->SuccBB));
        return EF(std::in_place_type<FeasibilityANDFormulaEF>, M, std::move(otherClause));
    }

    // Compose with another add
    if (auto *otherAddCons = secondFunction.template dyn_cast<FeasibilityAddConstrainEF>()) {
        // Create a new empty clause
        FeasibilityClause otherClause;

        // Add the incoming constraint to the clause, as we need to represent it as a clause when we compose with
        // other constraints or phis.
        otherClause.Constrs.push_back(LazyICmp(otherAddCons->ConstraintInst, otherAddCons->isTrueBranch));

        // Insert the newly constructed clause for the incoming constraint into the current clause, as we need to
        // represent the composition of the two constraints as a conjunction of the two constraints in a single clause.
        thisClause.Constrs.append(otherClause.Constrs.begin(), otherClause.Constrs.end());

        // Construct an and from the two clauses, as we want to represent the composition of the
        // two constraints as a conjunction of the two constraints in a single clause.
        return EF(std::in_place_type<FeasibilityANDFormulaEF>, M, std::move(thisClause));
    }

    // Compose with an and
    if (auto *otherAnd = secondFunction.template dyn_cast<FeasibilityANDFormulaEF>()) {
        // The incoming and already represents a conjunction of constraints and phis as a single clause,
        // so we can just combine the constraints of this edge function with the constraints in the incoming AND clause.

        FeasibilityClause incomingClause = otherAnd->Clause;
        incomingClause.Constrs.append(thisClause.Constrs.begin(), thisClause.Constrs.end());
        return EF(std::in_place_type<FeasibilityANDFormulaEF>, M, std::move(incomingClause));
    }

    // Compose with an OR.
    if (auto *otherOr = secondFunction.template dyn_cast<FeasibilityORFormulaEF>()) {
        // We need to compose this constraint with all clauses in the incoming OR
        auto orClauses = otherOr->Clauses;

        for (auto &clause : orClauses) {
            clause.Constrs.append(thisClause.Constrs.begin(), thisClause.Constrs.end());
        }

        return EF(std::in_place_type<FeasibilityORFormulaEF>, M, std::move(orClauses));
    }

    llvm::errs() << "ALARM in FeasibilityAddConstrainEF::compose: We are trying to compose an edge function of type FeasibilityAddConstrainEF with an edge function of an unsupported type. This should not happen, as the analysis should only produce edge functions of the supported types. "
                    "Please check the implementation of the analysis and make sure that it only produces edge functions of the supported types.\n";
    return secondFunction;
}

EF FeasibilityAddConstrainEF::join(psr::EdgeFunctionRef<FeasibilityAddConstrainEF> thisFunc, const psr::EdgeFunction<l_t> &secondFunction) {

    // Edge identy do nothing
    if (secondFunction.template isa<psr::EdgeIdentity<l_t>>()) {
        return EF(std::in_place_type<psr::EdgeIdentity<l_t>>);
    }

    // Top edge function acts as absorbing element for joining.
    // As we are calculaling A v T = T
    if (secondFunction.template isa<FeasibilityAllTopEF>()) {
        return EF(std::in_place_type<FeasibilityAllTopEF>);
    }

    // Bottom edge function acts as neutral element for joining,
    // as it maps any input to itself A V ⊥ = A
    if (secondFunction.template isa<FeasibilityAllBottomEF>()) {
        //llvm::errs() << "Joining FeasibilityAddConstrainEF with FeasibilityAllBottomEF. Result is the original FeasibilityAddConstrainEF, as A V ⊥ = A\n";
        return EF{thisFunc};
    }

    auto *M = thisFunc->manager;

    // Create new clause for this constraint that we want to add,
    // as we need to represent it as a clause when we compose with other constraints or phis.
    FeasibilityClause thisClause;
    // Add the constraint represented by this edge function to the clause, as we need to represent it as a
    // clause when we compose with other constraints or phis.
    thisClause.Constrs.push_back(LazyICmp(thisFunc->ConstraintInst, thisFunc->isTrueBranch));

    // Compose with an incoming phi
    if (auto *otherPhi = secondFunction.template dyn_cast<FeasibilityPHITranslateEF>()) {
        // We need to represent the phi translation as a clause and add the constraint of this edge function to this clause,
        FeasibilityClause phiClause;
        // Push this phi translation step to the clause's PHI chain, as it needs to be applied before the constraints in the clause.
        phiClause.PhiChain.push_back(PhiStep(otherPhi->PredBB, otherPhi->SuccBB));

        // Construct a new vector of clauses we want to OR together
        llvm::SmallVector<FeasibilityClause, 4> clauses;

        // Add the present clause to the or vector
        clauses.push_back(std::move(thisClause));
        // Add the newly constructed phi translation step to the or vector
        clauses.push_back(std::move(phiClause));

        return EF(std::in_place_type<FeasibilityORFormulaEF>, M, std::move(clauses));
    }

    // Compose with another add
    if (auto *otherAddCons = secondFunction.template dyn_cast<FeasibilityAddConstrainEF>()) {
        // Create a brand new clause
        FeasibilityClause addClause;
        // Add the constraint of the incoming add as lazy ICMP based constraint to the clause.
        addClause.Constrs.push_back(LazyICmp(otherAddCons->ConstraintInst, otherAddCons->isTrueBranch));

        // Create a new or clause vector and add this clause and the newly constructed clause for the incoming add to
        // this vector, as we want to represent the join of the two constraints as a disjunction
        // of the two constraints in separate clauses.
        llvm::SmallVector<FeasibilityClause, 4> clauses;
        clauses.push_back(std::move(thisClause));
        clauses.push_back(std::move(addClause));

        return EF(std::in_place_type<FeasibilityORFormulaEF>, M, std::move(clauses));
    }

    // Compose with an and
    if (auto *otherAnd = secondFunction.template dyn_cast<FeasibilityANDFormulaEF>()) {
        // Create a new or vector
        llvm::SmallVector<FeasibilityClause, 4> clauses;

        // Add this clause and the clause of the incoming and to this vector
        clauses.push_back(std::move(thisClause));
        clauses.push_back(otherAnd->Clause);
        return EF(std::in_place_type<FeasibilityORFormulaEF>, M, std::move(clauses));
    }

    // Compose with an OR.
    if (auto *otherOr = secondFunction.template dyn_cast<FeasibilityORFormulaEF>()) {
        // Get the incoming clauses
        auto clauses = otherOr->Clauses;

        // Add this clause to the incoming clauses
        clauses.push_back(std::move(thisClause));
        return EF(std::in_place_type<FeasibilityORFormulaEF>, M, std::move(clauses));
    }

    llvm::errs() << "ALARM in FeasibilityAddConstrainEF::join: We are trying to join an edge function of type FeasibilityAddConstrainEF with an edge function of an unsupported type. This should not happen, as the analysis should only produce edge functions of the supported types. "
                    "Please check the implementation of the analysis and make sure that it only produces edge functions of the supported types.\n";
    return EF(secondFunction);
}

// =====================================================================================================================
// FeasibilityANDFormulaEF

l_t FeasibilityANDFormulaEF::computeTarget(const l_t &source) const {
    auto *M = manager ? manager : source.manager;

    //llvm::errs() << "\t => source kind is " << (source.isTop() ? "Top" : (source.isBottom() ? "Bottom" : "Normal")) << ", formula ID is " << source.formularID << " and environment ID is " << source.environmentID << "\n";
    if (source.isBottom()) {
        return source;
    }

    // Apply PHI translations first to get correct SSA names for constraint creation
    const uint32_t outEnv = Util::applyPhiChain(M, source.environmentID, Clause.PhiChain);
    uint32_t pathConditionId = source.formularID;

    // Iterate over the lazy constraints in the clause and create the corresponding formulas, then add them to the path condition via AND.
    for (const auto &lazyConstr : Clause.Constrs) {
        // Dummy check if the ICMP contained in the lazy constraint is valid.
        // This should not be necessary, as we should only create FeasibilityANDFormulaEF with valid LazyICmp
        // constraints, but we check it just to be sure and to avoid potential crashes.
        if (!lazyConstr.I) {
            llvm::errs() << "ALARM in FeasibilityANDFormulaEF::computeTarget: LazyICmp has null instruction. This should not happen, as we should only create FeasibilityANDFormulaEF with valid LazyICmp constraints. Please check the implementation of the analysis and make sure that we are only creating FeasibilityANDFormulaEF with valid LazyICmp constraints.\n";
            continue;
        }

        // Create a new constraint from the lazy constraint,
        // the branch condition (true or false) and the environment after applying the PHI translations.
        z3::expr newlyConstructedExpression = Util::createConstraintFromICmp(M, lazyConstr.I, lazyConstr.TrueEdge, outEnv);
        const uint32_t newConstrId = Util::findOrAddFormulaId(M, newlyConstructedExpression);

        // Create an and from the path condition contained in the lattice element and the newly constructed
        // constraint, as we want to represent the composition of the
        pathConditionId = M->mkAnd(pathConditionId, newConstrId);

        if (!M->isSat(pathConditionId)) {
            auto out = source;
            out.kind = l_t::Kind::Bottom;
            out.formularID = source.bottomId;
            out.environmentID = outEnv;  // keep for debugging
            return out;
        }
    }

    auto out = source;
    out.formularID = pathConditionId;
    out.environmentID = outEnv; // keep for debugging
    if (out.kind == l_t::Kind::Top) {
        out.kind = l_t::Kind::Normal;
    }

    return out;
}

EF FeasibilityANDFormulaEF::compose(psr::EdgeFunctionRef<FeasibilityANDFormulaEF> thisFunc, const EF &secondFunction) {
    // Identiy do nothing
    if (secondFunction.template isa<psr::EdgeIdentity<l_t>>()) {
        return EF(thisFunc);
    }

    // Bottom kills everything
    if (secondFunction.template isa<FeasibilityAllBottomEF>()) {
        return EF(std::in_place_type<FeasibilityAllBottomEF>);
    }

    // Top kills everything
    if (secondFunction.template isa<FeasibilityAllTopEF>()) {
        return EF(std::in_place_type<FeasibilityAllTopEF>);
    }

    auto *M = thisFunc->manager;

    // Phi Case
    if (auto *otherPhi = secondFunction.template dyn_cast<FeasibilityPHITranslateEF>()) {
        // Create a new clause from the incoming phi information
        FeasibilityClause phiClause = Util::clauseFromPhi(otherPhi->PredBB, otherPhi->SuccBB);
        // Merge this clause and the phi clause by concatenating the PHI chains and the constraint vectors,
        // as we want to represent the composition of the two edge functions as a conjunction of the
        // constraints and phi translations in both clauses.
        FeasibilityClause mergedClause = Util::conjClauses(thisFunc->Clause, phiClause);

        return EF(std::in_place_type<FeasibilityANDFormulaEF>, M, std::move(mergedClause));
    }

    // Add constraint case
    if (auto *otherAdd = secondFunction.template dyn_cast<FeasibilityAddConstrainEF>()) {
        // Create a new clause from the incoming add information
        FeasibilityClause addClause = Util::clauseFromIcmp(otherAdd->ConstraintInst, otherAdd->isTrueBranch);
        // Merge this clause and the add clause by concatenating the PHI chains and the constraint vectors,
        // as we want to represent the composition of the two edge functions as a conjunction of the
        // constraints and phi translations in both clauses.
        FeasibilityClause mergedClause = Util::conjClauses(thisFunc->Clause, addClause);

        return EF(std::in_place_type<FeasibilityANDFormulaEF>, M, std::move(mergedClause));
    }

    // AND case
    if (auto *otherAnd = secondFunction.template dyn_cast<FeasibilityANDFormulaEF>()) {
        // Merge the two clauses by concatenating the PHI chains and the constraint vectors,
        // as we want to represent the composition of the two edge functions as a conjunction of the
        // constraints and phi translations in both clauses.
        FeasibilityClause mergedClause = Util::conjClauses(thisFunc->Clause, otherAnd->Clause);
        return EF(std::in_place_type<FeasibilityANDFormulaEF>, M, std::move(mergedClause));
    }

    // OR case
    if (auto *otherOr = secondFunction.template dyn_cast<FeasibilityORFormulaEF>()) {
        // We need to merge this clause with all clauses in the incoming OR
        llvm::SmallVector<FeasibilityClause, 4> mergedClauses;
        mergedClauses.reserve(otherOr->Clauses.size());

        for (const auto &orClause : otherOr->Clauses) {
            mergedClauses.push_back(Util::conjClauses(thisFunc->Clause, orClause));
        }

        return EF(std::in_place_type<FeasibilityORFormulaEF>, M, std::move(mergedClauses));
    }

    llvm::errs() << "ALARM in FeasibilityANDFormulaEF::compose: We are trying to compose an edge function of type FeasibilityANDFormulaEF with an edge function of an unsupported type. This should not happen, as the analysis should only produce edge functions of the supported types. "
                    "Please check the implementation of the analysis and make sure that it only produces edge functions of the supported types.\n";
    return EF(secondFunction);
}

EF FeasibilityANDFormulaEF::join(psr::EdgeFunctionRef<FeasibilityANDFormulaEF> thisFunc, const psr::EdgeFunction<l_t> &otherFunc) {

    // Identity case. Do nothing
    if (otherFunc.template isa<psr::EdgeIdentity<l_t>>()) {
        return EF(std::in_place_type<psr::EdgeIdentity<l_t>>);
    }

    // Top case. A V ⊤ = ⊤
    if (otherFunc.template isa<FeasibilityAllTopEF>() || otherFunc.template isa<psr::AllTop<l_t>>()) {
        return EF(std::in_place_type<FeasibilityAllTopEF>);
    }

    // Bottom case. A V ⊥ = A
    if (otherFunc.template isa<FeasibilityAllBottomEF>() || otherFunc.template isa<psr::AllBottom<l_t>>()) {
        return EF(thisFunc);
    }

    auto *M = thisFunc->manager;

    // Phi Case.
    if (auto *otherphi = otherFunc.template dyn_cast<FeasibilityPHITranslateEF>()) {
        // Create a new clauses vector for or operations
        llvm::SmallVector<FeasibilityClause, 4> clauses;
        // Add this clause
        clauses.push_back(thisFunc->Clause);
        // Add the incoming phi as clause
        clauses.push_back(Util::clauseFromPhi(otherphi->PredBB, otherphi->SuccBB));

        return EF(std::in_place_type<FeasibilityORFormulaEF>, M, std::move(clauses));
    }

    // Add Case.
    if (auto *otherAdd = otherFunc.template dyn_cast<FeasibilityAddConstrainEF>()) {
        // Create a new clauses vector for or operations
        llvm::SmallVector<FeasibilityClause, 4> clauses;
        // Add this clause
        clauses.push_back(thisFunc->Clause);
        // Add the incoming add as clause
        clauses.push_back(Util::clauseFromIcmp(otherAdd->ConstraintInst, otherAdd->isTrueBranch));

        return EF(std::in_place_type<FeasibilityORFormulaEF>, M, std::move(clauses));
    }

    // AND case
    if (auto *otherAnd = otherFunc.template dyn_cast<FeasibilityANDFormulaEF>()) {
        // Create a new clauses vector
        llvm::SmallVector<FeasibilityClause, 4> clauses;
        // Add this clause
        clauses.push_back(thisFunc->Clause);
        // Add the incomin add clause
        clauses.push_back(otherAnd->Clause);
        return EF(std::in_place_type<FeasibilityORFormulaEF>, M, std::move(clauses));
    }

    // OR case
    if (auto *otherOr = otherFunc.template dyn_cast<FeasibilityORFormulaEF>()) {
        auto existingClauses = otherOr->Clauses;
        existingClauses.push_back(thisFunc->Clause);
        return EF(std::in_place_type<FeasibilityORFormulaEF>, M, std::move(existingClauses));
    }

    llvm::errs() << "ALARM in FeasibilityANDFormulaEF::join: unsupported otherFunc\n";
    return EF(otherFunc);
}

// =====================================================================================================================
// FeasibilityORFormulaEF

l_t FeasibilityORFormulaEF::computeTarget(const l_t &source) const {
    auto *M = manager ? manager : source.manager;

    if (source.isBottom()) {
        return source;
    }

    // Start our computation with the path condition ID of the incoming element. Start with bottom ID, as we will
    // build up the formula from there by adding constraints and PHI translations via OR operations.
    uint32_t accumulatedPathConditionId = source.bottomId;
    // Keep track of the sat state of the disjunkt elements. If all of them are UNSAT, we return a bottom element
    bool isAnyOfTheClausesSatisfied = false;

    // Iterate over the clauses in the OR formula and create the corresponding formulas, then add them to the path condition via OR.
    for (const auto &clause : Clauses) {
        FeasibilityANDFormulaEF andEF(M, clause);
        l_t out = andEF.computeTarget(source);

        if (out.isBottom()) {
            continue;
        }

        accumulatedPathConditionId = M->mkOr(accumulatedPathConditionId, out.formularID);
        isAnyOfTheClausesSatisfied = true;
    }

    // Check if none of the clauses were satisfied.
    // In this case, we return a bottom element, as we have a disjunction of unsatisfiable formulas,
    // which is itself unsatisfiable.
    if (!isAnyOfTheClausesSatisfied) {
        auto out = source;
        out.kind = l_t::Kind::Bottom;
        out.formularID = l_t::bottomId;
        return out;
    }

    /*if (!M->isSat(accumulatedPathConditionId)) {
        auto out = source;
        out.kind = l_t::Kind::Bottom;
        out.formularID = source.bottomId;
        return out;
    }*/

    auto out = source;
    out.formularID = accumulatedPathConditionId;

    // Environment after OR is not uniquely defined; each clause applies its own phi chain internally.
    // Keep env unchanged.
    if (out.kind == l_t::Kind::Top) {
        out.kind = l_t::Kind::Normal;
    }
    return out;

}

EF FeasibilityORFormulaEF::compose(psr::EdgeFunctionRef<FeasibilityORFormulaEF> thisFunc, const EF &secondFunction) {
    // Identiy do nothing
    if (secondFunction.template isa<psr::EdgeIdentity<l_t>>()) {
        return EF(thisFunc);
    }

    // Bottom kills everything
    if (secondFunction.template isa<FeasibilityAllBottomEF>()) {
        return EF(std::in_place_type<FeasibilityAllBottomEF>);
    }

    // Top kills everything
    if (secondFunction.template isa<FeasibilityAllTopEF>()) {
        return EF(std::in_place_type<FeasibilityAllTopEF>);
    }

    auto *M = thisFunc->manager;

    // Phi case
    if (auto *otherPhi = secondFunction.template dyn_cast<FeasibilityPHITranslateEF>()) {
        // Create a new clause from the incoming phi information
        FeasibilityClause phiClause = Util::clauseFromPhi(otherPhi->PredBB, otherPhi->SuccBB);

        // Create new clauses vector
        llvm::SmallVector<FeasibilityClause, 4> clauses;
        clauses.reserve(thisFunc->Clauses.size());

        // Iterate over the contained clauses
        for (const auto &clause : thisFunc->Clauses) {
            // Join each clause with the incoming phi clause by concatenating the PHI chains and the constraint vectors,
            clauses.push_back(Util::conjClauses(clause, phiClause));
        }

        return EF(std::in_place_type<FeasibilityORFormulaEF>, M, std::move(clauses));
    }

    // Add constraint case
    if (auto *otherAdd = secondFunction.template dyn_cast<FeasibilityAddConstrainEF>()) {
        // Create a new clause from the incoming phi information
        FeasibilityClause addClause = Util::clauseFromIcmp(otherAdd->ConstraintInst, otherAdd->isTrueBranch);

        // Create new clauses vector
        llvm::SmallVector<FeasibilityClause, 4> clauses;
        clauses.reserve(thisFunc->Clauses.size());

        // Iterate over the contained clauses
        for (const auto &clause : thisFunc->Clauses) {
            // Join each clause with the incoming phi clause by concatenating the PHI chains and the constraint vectors,
            clauses.push_back(Util::conjClauses(clause, addClause));
        }

        return EF(std::in_place_type<FeasibilityORFormulaEF>, M, std::move(clauses));
    }

    // AND case
    if (auto *otherAnd = secondFunction.template dyn_cast<FeasibilityANDFormulaEF>()) {
        // Create new clauses vector
        llvm::SmallVector<FeasibilityClause, 4> clauses;
        clauses.reserve(thisFunc->Clauses.size());

        // Iterate over the contained clauses
        for (const auto &clause : thisFunc->Clauses) {
            // Merge each clause with the incoming and clause by concatenating the PHI chains and the constraint vectors,
            clauses.push_back(Util::conjClauses(clause, otherAnd->Clause));
        }

        return EF(std::in_place_type<FeasibilityORFormulaEF>, M, std::move(clauses));
    }

    // OR case
    if (auto *otherOr = secondFunction.template dyn_cast<FeasibilityORFormulaEF>()) {
        // Create new clauses vector
        llvm::SmallVector<FeasibilityClause, 4> clauses;
        clauses.reserve(thisFunc->Clauses.size() + otherOr->Clauses.size());

        // Iterate over the contained clauses
        for (const auto &presentClause : thisFunc->Clauses) {
            // Iterate over the incoming clauses
            for (const auto &inComingClause : otherOr->Clauses) {
                clauses.push_back(Util::conjClauses(presentClause, inComingClause));
            }
        }

        return EF(std::in_place_type<FeasibilityORFormulaEF>, M, std::move(clauses));
    }

    llvm::errs() << "ALARM in FeasibilityORFormulaEF::compose: We are trying to compose an edge function of type FeasibilityORFormulaEF with an edge function of an unsupported type. This should not happen, as the analysis should only produce edge functions of the supported types. "
                    "Please check the implementation of the analysis and make sure that it only produces edge functions of the supported types.\n";
    return secondFunction;
}

EF FeasibilityORFormulaEF::join(psr::EdgeFunctionRef<FeasibilityORFormulaEF> thisFunc, const psr::EdgeFunction<l_t> &secondFunction) {
    // Identiy do nothing
    if (secondFunction.template isa<psr::EdgeIdentity<l_t>>()) {
        return EF(thisFunc);
    }

    // Bottom kills everything
    if (secondFunction.template isa<FeasibilityAllBottomEF>()) {
        return EF(std::in_place_type<FeasibilityAllBottomEF>);
    }

    // Top kills everything
    if (secondFunction.template isa<FeasibilityAllTopEF>()) {
        return EF(std::in_place_type<FeasibilityAllTopEF>);
    }

    auto *M = thisFunc->manager;

    // Phi case.
    if (auto *otherPhi = secondFunction.template dyn_cast<FeasibilityPHITranslateEF>()) {
        // Get the existing clauses
        auto clauses = thisFunc->Clauses;
        // Push the incoming phi as new clause
        clauses.push_back(Util::clauseFromPhi(otherPhi->PredBB, otherPhi->SuccBB));
        return EF(std::in_place_type<FeasibilityORFormulaEF>, M, std::move(clauses));
    }

    // Add case
    if (auto *otherAdd = secondFunction.template dyn_cast<FeasibilityAddConstrainEF>()) {
        // Get the existing clauses
        auto clauses = thisFunc->Clauses;
        // Push the incoming phi as new clause
        clauses.push_back(Util::clauseFromIcmp(otherAdd->ConstraintInst, otherAdd->isTrueBranch));
        return EF(std::in_place_type<FeasibilityORFormulaEF>, M, std::move(clauses));
    }

    // And case
    if (auto *otherAnd = secondFunction.template dyn_cast<FeasibilityANDFormulaEF>()) {
        // Get the clauses
        auto clauses = thisFunc->Clauses;
        // Add the incoming and clause
        clauses.push_back(otherAnd->Clause);
        return EF(std::in_place_type<FeasibilityORFormulaEF>, M, std::move(clauses));
    }

    // Or case
    if (auto *otherOr = secondFunction.template dyn_cast<FeasibilityORFormulaEF>()) {
        // Get the clauses
        auto clauses = thisFunc->Clauses;
        // Insert all incoming or clauses
        clauses.append(otherOr->Clauses.begin(), otherOr->Clauses.end());
        return EF(std::in_place_type<FeasibilityORFormulaEF>, M, std::move(clauses));
    }

    llvm::errs() << "ALARM in FeasibilityORFormulaEF::compose: We are trying to compose an edge function of type FeasibilityORFormulaEF with an edge function of an unsupported type. This should not happen, as the analysis should only produce edge functions of the supported types. "
                    "Please check the implementation of the analysis and make sure that it only produces edge functions of the supported types.\n";
    return secondFunction;
}

} // namespace Feasibility
