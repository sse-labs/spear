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

#include "FeasibilityEdgeFunctionMemo.h"
#include "FeasibilityElement.h"

namespace Feasibility {

using l_t = Feasibility::FeasibilityElement;
using EF = psr::EdgeFunction<l_t>;

static inline std::size_t hashPtr(const void *p) noexcept {
  return std::hash<const void *>{}(p);
}

// 64-bit mix (boost-like)
static inline std::size_t hashCombine(std::size_t h, std::size_t x) noexcept {
  // 64-bit constant works fine on 32-bit too (it will truncate)
  h ^= x + 0x9e3779b97f4a7c15ULL + (h << 6) + (h >> 2);
  return h;
}

// One atomic constraint, evaluated lazily under the current env.
struct LazyICmp {
  const llvm::ICmpInst *I = nullptr;
  bool TrueEdge = true;

  LazyICmp() = default;
  LazyICmp(const llvm::ICmpInst *Inst, bool T) : I(Inst), TrueEdge(T) {}

  bool operator==(const LazyICmp &) const = default;

  std::size_t hash() const noexcept {
    std::size_t h = hashPtr(I);
    h = hashCombine(h, std::hash<bool>{}(TrueEdge));
    return h;
  }
};

// One PHI translation step (Pred -> Succ) to apply via applyPhiPack.
struct PhiStep {
  const llvm::BasicBlock *PredBB = nullptr;
  const llvm::BasicBlock *SuccBB = nullptr;

  PhiStep() = default;
  PhiStep(const llvm::BasicBlock *P, const llvm::BasicBlock *S) : PredBB(P), SuccBB(S) {}

  bool operator==(const PhiStep &) const = default;

  std::size_t hash() const noexcept {
    std::size_t h = hashPtr(PredBB);
    h = hashCombine(h, hashPtr(SuccBB));
    return h;
  }
};

// DNF helper clause: conjunction of phi-steps and constraints.
struct FeasibilityClause {
  llvm::SmallVector<PhiStep, 2> PhiChain; // optional
  llvm::SmallVector<LazyICmp, 4> Constrs; // conjunction

  bool operator==(const FeasibilityClause &) const = default;

  std::size_t hash() const noexcept {
    // Tag the two sequences so [PhiChain=A, Constrs=B] != [PhiChain=B, Constrs=A]
    std::size_t h = 0xC1A0C1A0u;

    h = hashCombine(h, std::hash<unsigned>{}((unsigned)PhiChain.size()));
    for (const auto &s : PhiChain) {
      h = hashCombine(h, s.hash());
    }

    h = hashCombine(h, 0xBADC0FFEu);
    h = hashCombine(h, std::hash<unsigned>{}((unsigned)Constrs.size()));
    for (const auto &c : Constrs) {
      h = hashCombine(h, c.hash());
    }

    return h;
  }
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

    mutable ComputeTargetMemo<FeasibilityElement> Memo;

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

    bool operator==(const FeasibilityAddConstrainEF &other) const {
        // We consider two FeasibilityAddConstrainEFs to be equal if they add the same constraint instruction with the same branch condition, regardless of the manager pointer (which is not relevant for equality).
        return ConstraintInst == other.ConstraintInst && isTrueBranch == other.isTrueBranch;
    };

    bool isConstant() const noexcept { return false; };
};


// Adds a *formula id* to the incoming PC via AND:  x ↦ x ∧ Cid
// This is our sequential operator
struct FeasibilityANDFormulaEF {
    using l_t = Feasibility::l_t;

    mutable ComputeTargetMemo<FeasibilityElement> Memo;

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

    bool operator==(const FeasibilityANDFormulaEF &other) const {
        // We consider two FeasibilityANDFormulaEFs to be equal if they have the same clause (i.e. represent the same conjunction of constraints and phi translations), regardless of the manager pointer (which is not relevant for equality).
        return Clause == other.Clause;
    };
    bool isConstant() const noexcept { return false; }
};

// Computes: x ↦ OR_i ( x ∧ Ci )
// This is our join operator
struct FeasibilityORFormulaEF {
    using l_t = Feasibility::l_t;

    mutable ComputeTargetMemo<FeasibilityElement> Memo;

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

    bool operator==(const FeasibilityORFormulaEF &other) const {
        // We consider two FeasibilityORFormulaEFs to be equal if they have the same set of clauses (i.e. represent the same DNF formula), regardless of the manager pointer (which is not relevant for equality).
        // Note that we do not require the clauses to be in the same order, as the order of disjuncts in a DNF formula does not matter.
        if (Clauses.size() != other.Clauses.size()) {
            return false;
        }
        // Check that every clause in this->Clauses has an equal clause in other.Clauses
        for (const auto &c : Clauses) {
            bool foundEqual = false;
            for (const auto &oc : other.Clauses) {
                if (c == oc) {
                    foundEqual = true;
                    break;
                }
            }
            if (!foundEqual) {
                return false;
            }
        }
        return true;
    }
    bool isConstant() const noexcept { return false; }
};

/**
 * Updates the environment id
 */
struct FeasibilityPHITranslateEF {
    using l_t = Feasibility::l_t;

    mutable ComputeTargetMemo<FeasibilityElement> Memo;


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

    bool operator==(const FeasibilityPHITranslateEF &other) const {
        // We consider two FeasibilityPHITranslateEFs to be equal if they represent the same edge identity (i.e. have the same PredBB and SuccBB), regardless of the manager pointer (which is not relevant for equality).
        return PredBB == other.PredBB && SuccBB == other.SuccBB;
    }
    bool isConstant() const noexcept { return false; }
};

// ============================================================================
// Lazy EF nodes to avoid OR∘OR distribution blow-ups

/// Represents function composition without expanding (f ∘ g).
/// computeTarget(x) = f(g(x)).
struct FeasibilityComposeEF {
    using l_t = Feasibility::l_t;

    mutable ComputeTargetMemo<FeasibilityElement> Memo;

    FeasibilityAnalysisManager *manager = nullptr;
    EF First;   // applied second (outer)
    EF Second;  // applied first  (inner)

    FeasibilityComposeEF() = default;

    FeasibilityComposeEF(FeasibilityAnalysisManager *M, EF F, EF G)
        : manager(M), First(std::move(F)), Second(std::move(G)) {}

    [[nodiscard]] l_t computeTarget(const l_t &source) const;

    [[nodiscard]] static EF compose(psr::EdgeFunctionRef<FeasibilityComposeEF> thisFunc,
                                    const EF &secondFunction);

    [[nodiscard]] static EF join(psr::EdgeFunctionRef<FeasibilityComposeEF> thisFunc,
                                 const psr::EdgeFunction<l_t> &otherFunc);

    bool operator==(const FeasibilityComposeEF &other) const {
        // We consider two FeasibilityComposeEFs to be equal if they have the same First and Second functions (i.e. represent the same composition), regardless of the manager pointer (which is not relevant for equality).
        return First == other.First && Second == other.Second;
    }
    bool isConstant() const noexcept { return false; }
};

/// Represents pointwise join without expanding into DNF.
/// (f ⊔ g)(x) = f(x) ⊔ g(x)  where ⊔ on l_t corresponds to OR of PCs.
struct FeasibilityJoinEF {
    using l_t = Feasibility::l_t;

    mutable ComputeTargetMemo<FeasibilityElement> Memo;

    FeasibilityAnalysisManager *manager = nullptr;
    EF Left;
    EF Right;

    FeasibilityJoinEF() = default;

    FeasibilityJoinEF(FeasibilityAnalysisManager *M, EF L, EF R)
        : manager(M), Left(std::move(L)), Right(std::move(R)) {}

    [[nodiscard]] l_t computeTarget(const l_t &source) const;

    [[nodiscard]] static EF compose(psr::EdgeFunctionRef<FeasibilityJoinEF> thisFunc,
                                    const EF &secondFunction);

    [[nodiscard]] static EF join(psr::EdgeFunctionRef<FeasibilityJoinEF> thisFunc,
                                 const psr::EdgeFunction<l_t> &otherFunc);

    bool operator==(const FeasibilityJoinEF &other) const {
        // We consider two FeasibilityJoinEFs to be equal if they have the same Left and Right functions (i.e. represent the same pointwise join), regardless of the manager pointer (which is not relevant for equality).
        return Left == other.Left && Right == other.Right;
    }
    bool isConstant() const noexcept { return false; }
};

}  // namespace Feasibility

// std::hash specializations (so you can stick these in unordered_map/unordered_set)
namespace std {
template <> struct hash<Feasibility::LazyICmp> {
    std::size_t operator()(const Feasibility::LazyICmp &x) const noexcept { return x.hash(); }
};
template <> struct hash<Feasibility::PhiStep> {
    std::size_t operator()(const Feasibility::PhiStep &x) const noexcept { return x.hash(); }
};
template <> struct hash<Feasibility::FeasibilityClause> {
    std::size_t operator()(const Feasibility::FeasibilityClause &x) const noexcept { return x.hash(); }
};
} // namespace std


#endif  // SPEAR_FEASIBILITYEDGEFUNCTION_H

