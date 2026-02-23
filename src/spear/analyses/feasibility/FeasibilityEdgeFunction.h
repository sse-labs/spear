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
    struct ClauseStep {
        enum class Kind : uint8_t { Phi, ICmp };
        Kind K;
        PhiStep Phi;
        LazyICmp ICmp;

        static ClauseStep mkPhi(const llvm::BasicBlock *P, const llvm::BasicBlock *S) {
            ClauseStep st;
            st.K = Kind::Phi;
            st.Phi = PhiStep(P, S);
            return st;
        }
        static ClauseStep mkICmp(const llvm::ICmpInst *I, bool T) {
            ClauseStep st;
            st.K = Kind::ICmp;
            st.ICmp = LazyICmp(I, T);
            return st;
        }

        bool operator==(const ClauseStep &o) const = default;

        std::size_t hash() const noexcept {
            std::size_t h = 0x51;
            h = hashCombine(h, std::hash<unsigned>{}(unsigned(K)));
            if (K == Kind::Phi) {
                h = hashCombine(h, Phi.hash());
            } else {
                h = hashCombine(h, ICmp.hash());
            }
            return h;
        }
    };

    struct FeasibilityClause {
        llvm::SmallVector<ClauseStep, 8> Steps;

        bool operator==(const FeasibilityClause &) const = default;

        std::size_t hash() const noexcept {
            std::size_t h = 0xC1A0C1A0u;
            h = hashCombine(h, std::hash<unsigned>{}((unsigned)Steps.size()));
            for (const auto &s : Steps) h = hashCombine(h, s.hash());
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
        Clause.Steps.push_back(ClauseStep::mkICmp(I, OnTrueEdge));
    }

    // Convenience: phi-only clause
    FeasibilityANDFormulaEF(FeasibilityAnalysisManager *M,
                            const llvm::BasicBlock *Pred,
                            const llvm::BasicBlock *Succ)
        : manager(M) {
        Clause.Steps.push_back(ClauseStep::mkPhi(Pred, Succ));
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

static inline bool isIdEF(const EF &ef) noexcept {
    return ef.template isa<psr::EdgeIdentity<l_t>>();
}
static inline bool isAllTopEF(const EF &ef) noexcept {
    return ef.template isa<FeasibilityAllTopEF>() || ef.template isa<psr::AllTop<l_t>>();
}

constexpr bool FDBG = true;

static inline FeasibilityAnalysisManager *pickManager(FeasibilityAnalysisManager *M, const l_t &source) {
    // If the source is bottom, we can pick any manager, because the result will be bottom anyway (and thus not depend on the manager).
    // This allows us to avoid unnecessary dependencies on the manager in some edge functions.
    if (source.isBottom()) {
        return M;
    }
    return source.getManager();
}

static inline const char *kindStr(Feasibility::l_t::Kind K) {
    using Kt = Feasibility::l_t::Kind;
    switch (K) {
        case Kt::Top:    return "Top";
        case Kt::Bottom: return "Bottom";
    }
    return "?";
}

static inline void dumpLatticeBrief(const char *Pfx,
                                    const Feasibility::l_t &x) {
    llvm::errs() << "[FDBG] " << Pfx << " kind=" << kindStr(x.getKind()) << "\n";
}

// Best-effort EF name printer (no RTTI needed; uses isa<> checks).
static inline const char *efName(const EF &ef) {
    if (ef.template isa<psr::EdgeIdentity<l_t>>()) return "Identity";
    if (ef.template isa<FeasibilityAllTopEF>() || ef.template isa<psr::AllTop<l_t>>()) return "AllTopEF";
    if (ef.template isa<FeasibilityAllBottomEF>() || ef.template isa<psr::AllBottom<l_t>>()) return "AllBottomEF";
    if (ef.template isa<FeasibilityPHITranslateEF>()) return "PHITranslateEF";
    if (ef.template isa<FeasibilityANDFormulaEF>()) return "ANDFormulaEF";
    if (ef.template isa<FeasibilityComposeEF>()) return "ComposeEF";
    return "EF<?>"; // fallback
}

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

