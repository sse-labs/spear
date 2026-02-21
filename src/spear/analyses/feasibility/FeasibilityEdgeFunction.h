/*
 * Copyright (c) 2026 Maximilian Krebs
 * All rights reserved.
 */

#ifndef SPEAR_FEASIBILITYEDGEFUNCTION_H
#define SPEAR_FEASIBILITYEDGEFUNCTION_H

#include <phasar/DataFlow/IfdsIde/EdgeFunction.h>
#include <llvm/IR/Value.h>           // add if not already pulled transitively
#include <z3++.h>                    // add if not already pulled transitively

#include <utility>
#include <llvm/IR/Instructions.h>
#include <phasar/DataFlow/IfdsIde/EdgeFunctionUtils.h>

#include "FeasibilityElement.h"

namespace Feasibility {

using l_t = Feasibility::FeasibilityElement;
using EF = psr::EdgeFunction<l_t>;

/**
 * Helper structs that realize our flow of data
 */

// One atomic constraint, evaluated lazily under the current env.
struct LazyICmp {
    const llvm::ICmpInst *I = nullptr;
    bool TrueEdge = true;

    LazyICmp() = default;
    LazyICmp(const llvm::ICmpInst *I, bool TrueEdge) : I(I), TrueEdge(TrueEdge) {}

    bool operator==(const LazyICmp &) const = default;
};

// One PHI translation step (Pred -> Succ) to apply via applyPhiPack.
struct PhiStep {
    const llvm::BasicBlock *PredBB = nullptr;
    const llvm::BasicBlock *SuccBB = nullptr;

    PhiStep() = default;
    PhiStep(const llvm::BasicBlock *P, const llvm::BasicBlock *S) : PredBB(P), SuccBB(S) {}

    bool operator==(const PhiStep &) const = default;
};

/**
 * DNF Helper clause.
 * Stores Phi translation steps and atomic constraints that we want to add to the path condition as a conjunction.
 * We use this struct to represent the clauses that we want to add to the path condition in the FeasibilityANDFormulaEF,
 * and to represent the disjuncts in the Feas
 */
struct FeasibilityClause {
    llvm::SmallVector<PhiStep, 2> PhiChain;      // optional
    llvm::SmallVector<LazyICmp, 4> Constrs;      // conjunction

    bool operator==(const FeasibilityClause &) const = default;
};


/**
 * Top edge function for the FeasibilityAnalysis. Maps any input to top.
 */
struct FeasibilityAllTopEF {
    using l_t = Feasibility::l_t;

    FeasibilityAllTopEF() {}

    [[nodiscard]] l_t computeTarget(const l_t &source) const;

    [[nodiscard]] static EF compose(psr::EdgeFunctionRef<FeasibilityAllTopEF>,
                                   const EF &secondFunction);

    [[nodiscard]] static EF join(psr::EdgeFunctionRef<FeasibilityAllTopEF> thisFunc,
                   const psr::EdgeFunction<l_t> &otherFunc);

    bool operator==(const FeasibilityAllTopEF &) const = default;

    bool isConstant() const noexcept { return false; };
};

/**
 * Bottom edge function for the FeasibilityAnalysis. Maps any input to bottom.
 */
struct FeasibilityAllBottomEF {
    using l_t = Feasibility::l_t;

    FeasibilityAllBottomEF() {}

    [[nodiscard]] l_t computeTarget(const l_t &source) const;

    [[nodiscard]] static EF compose(psr::EdgeFunctionRef<FeasibilityAllBottomEF>,
                                   const EF &secondFunction);

    [[nodiscard]] static EF join(psr::EdgeFunctionRef<FeasibilityAllBottomEF> thisFunc,
                   const psr::EdgeFunction<l_t> &otherFunc);

    bool operator==(const FeasibilityAllBottomEF &) const = default;

    bool isConstant() const noexcept { return false; };
};

/**
 * Top edge function for the FeasibilityAnalysis. Maps any input to top.
 */
struct FeasibilityAddConstrainEF {
    using l_t = Feasibility::l_t;

    FeasibilityAnalysisManager *manager = nullptr;
    const llvm::ICmpInst *ConstraintInst = nullptr; // The instruction that represents the constraint we want to add to the path condition
    bool isTrueBranch = false; // Whether we are in the true or false branch of the constraint instruction. This is needed to correctly construct the constraint formula from the ICmp instruction.

    FeasibilityAddConstrainEF(FeasibilityAnalysisManager *manager, const llvm::ICmpInst *icmp, bool isTrueBranch):
    manager(manager), ConstraintInst(icmp), isTrueBranch(isTrueBranch) {}

    [[nodiscard]] l_t computeTarget(const l_t &source) const;

    [[nodiscard]] static EF compose(psr::EdgeFunctionRef<FeasibilityAddConstrainEF>,
                                   const EF &secondFunction);

    [[nodiscard]] static EF join(psr::EdgeFunctionRef<FeasibilityAddConstrainEF> thisFunc,
                   const psr::EdgeFunction<l_t> &otherFunc);

    bool operator==(const FeasibilityAddConstrainEF &) const = default;

    bool isConstant() const noexcept { return false; };
};


// Adds a *formula id* to the incoming PC via AND:  x ↦ x ∧ Cid
// This is our sequential operator
struct FeasibilityANDFormulaEF {
    using l_t = Feasibility::l_t;

    FeasibilityAnalysisManager *manager = nullptr;

    // A conjunction of constraints and phi translations that we want to add to the path condition.
    // This represents a clause in a DNF formula.
    FeasibilityClause Clause;

    FeasibilityANDFormulaEF(FeasibilityAnalysisManager *M, FeasibilityClause C)
      : manager(M), Clause(std::move(C)) {}

    // Convenience: single icmp clause
    FeasibilityANDFormulaEF(FeasibilityAnalysisManager *M,
                            const llvm::ICmpInst *I,
                            bool OnTrueEdge)
        : manager(M) {
        Clause.Constrs.push_back(LazyICmp(I, OnTrueEdge));
    }

    // Convenience: phi-only clause
    FeasibilityANDFormulaEF(FeasibilityAnalysisManager *M,
                            const llvm::BasicBlock *Pred,
                            const llvm::BasicBlock *Succ)
        : manager(M) {
        Clause.PhiChain.push_back(PhiStep(Pred, Succ));
    }

    [[nodiscard]] l_t computeTarget(const l_t &source) const;

    [[nodiscard]] static EF compose(psr::EdgeFunctionRef<FeasibilityANDFormulaEF> thisFunc,
                                  const EF &secondFunction);

    [[nodiscard]] static EF join(psr::EdgeFunctionRef<FeasibilityANDFormulaEF> thisFunc,
                               const psr::EdgeFunction<l_t> &otherFunc);

    bool operator==(const FeasibilityANDFormulaEF &) const = default;
    bool isConstant() const noexcept { return false; }
};

// Computes: x ↦ OR_i ( x ∧ Ci )
// This is our join operator
struct FeasibilityORFormulaEF {
    using l_t = Feasibility::l_t;

    FeasibilityAnalysisManager *manager = nullptr;

    // Each clause represents a conjunction of constraints and phi translations that we want to add to the
    // path condition, and the OR formula represents the disjunction of these clauses.
    // Thus, we can represent any DNF formula over our constraints and phi translations using this struct.
    // We use a SmallVector here because we expect to have only a few disjuncts in practice,
    llvm::SmallVector<FeasibilityClause, 4> Clauses;

    FeasibilityORFormulaEF() = default;

    FeasibilityORFormulaEF(FeasibilityAnalysisManager *M,
                           llvm::SmallVector<FeasibilityClause, 4> Cs)
        : manager(M), Clauses(std::move(Cs)) {}

    [[nodiscard]] l_t computeTarget(const l_t &source) const;

    [[nodiscard]] static EF compose(psr::EdgeFunctionRef<FeasibilityORFormulaEF> thisFunc,
                                  const EF &secondFunction);

    [[nodiscard]] static EF join(psr::EdgeFunctionRef<FeasibilityORFormulaEF> thisFunc,
                               const psr::EdgeFunction<l_t> &otherFunc);

    bool operator==(const FeasibilityORFormulaEF &) const = default;
    bool isConstant() const noexcept { return false; }
};

/**
 * Updates the environment id
 */
struct FeasibilityPHITranslateEF {
    using l_t = Feasibility::l_t;

    FeasibilityAnalysisManager *manager = nullptr;

    // Edge identity of the translation: "incoming edge pred -> succ"
    const llvm::BasicBlock *PredBB = nullptr;
    const llvm::BasicBlock *SuccBB = nullptr;

    FeasibilityPHITranslateEF() = default;

    FeasibilityPHITranslateEF(FeasibilityAnalysisManager *M,
                              const llvm::BasicBlock *Pred,
                              const llvm::BasicBlock *Succ)
        : manager(M), PredBB(Pred), SuccBB(Succ) {}

    [[nodiscard]] l_t computeTarget(const l_t &source) const;

    [[nodiscard]] static EF compose(psr::EdgeFunctionRef<FeasibilityPHITranslateEF> thisFunc,
                                    const EF &secondFunction);

    [[nodiscard]] static EF join(psr::EdgeFunctionRef<FeasibilityPHITranslateEF> thisFunc,
                                 const psr::EdgeFunction<l_t> &otherFunc);

    bool operator==(const FeasibilityPHITranslateEF &) const = default;
    bool isConstant() const noexcept { return false; }
};

}  // namespace Feasibility


#endif  // SPEAR_FEASIBILITYEDGEFUNCTION_H

