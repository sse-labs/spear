#ifndef SPEAR_FEASIBILITYEDGEFUNCTION_H
#define SPEAR_FEASIBILITYEDGEFUNCTION_H

#include <phasar/DataFlow/IfdsIde/EdgeFunction.h>
#include <phasar/DataFlow/IfdsIde/EdgeFunctionUtils.h>

#include <llvm/ADT/SmallVector.h>
#include <llvm/IR/BasicBlock.h>
#include <llvm/IR/Instructions.h>
#include <llvm/Support/raw_ostream.h>

#include <z3++.h>

#include "FeasibilityElement.h"
#include "analyses/feasibility/util.h" // Util::createConstraintFromICmp

namespace Feasibility {

using l_t = FeasibilityElement;
using EF  = psr::EdgeFunction<l_t>;

// ============================================================================
// AllBottomEF: constant BOTTOM (unreachable)
// ============================================================================
struct FeasibilityAllBottomEF {
  using l_t = FeasibilityElement;
  [[nodiscard]] l_t computeTarget(const l_t &source) const;
  [[nodiscard]] static EF compose(psr::EdgeFunctionRef<FeasibilityAllBottomEF>,
                                  const EF &secondFunction);
  [[nodiscard]] static EF join(psr::EdgeFunctionRef<FeasibilityAllBottomEF>,
                               const psr::EdgeFunction<l_t> &otherFunc);

  bool operator==(const FeasibilityAllBottomEF &) const = default;
  bool isConstant() const noexcept { return false; }
};

// ============================================================================
// Fork B: AddAtomsEF is *lazy* over env and *phi-aware*.
//
// Each LazyAtom carries the CFG edge (Pred->Succ) whose PHIs must be applied
// before evaluating the ICmp atom.
// ============================================================================
struct LazyAtom {
  const llvm::BasicBlock *PredBB = nullptr;
  const llvm::BasicBlock *SuccBB = nullptr;
  const llvm::ICmpInst *I = nullptr;
  bool TrueEdge = true;

  LazyAtom() = default;
  LazyAtom(const llvm::BasicBlock *P, const llvm::BasicBlock *S,
           const llvm::ICmpInst *Inst, bool T)
      : PredBB(P), SuccBB(S), I(Inst), TrueEdge(T) {}

  bool operator==(const LazyAtom &o) const noexcept {
    return PredBB == o.PredBB && SuccBB == o.SuccBB && I == o.I &&
           TrueEdge == o.TrueEdge;
  }
};

// Adds a conjunction of (lazy) atoms to the current conjunction set.
// compose = concat atoms (second then this)
// join    = intersection of atoms (keep only guaranteed adds)
struct FeasibilityAddAtomsEF {
  using l_t = FeasibilityElement;
  FeasibilityAnalysisManager *manager = nullptr;
  llvm::SmallVector<LazyAtom, 4> Atoms;

  FeasibilityAddAtomsEF() = default;
  FeasibilityAddAtomsEF(FeasibilityAnalysisManager *M,
                        llvm::SmallVector<LazyAtom, 4> A)
      : manager(M), Atoms(std::move(A)) {}

  // Convenience: single atom on a specific edge Pred->Succ
  FeasibilityAddAtomsEF(FeasibilityAnalysisManager *M,
                        const llvm::BasicBlock *Pred,
                        const llvm::BasicBlock *Succ,
                        const llvm::ICmpInst *I,
                        bool OnTrueEdge)
      : manager(M) {
    Atoms.emplace_back(Pred, Succ, I, OnTrueEdge);
  }

  [[nodiscard]] l_t computeTarget(const l_t &source) const;

  [[nodiscard]] static EF compose(psr::EdgeFunctionRef<FeasibilityAddAtomsEF> thisFunc,
                                  const EF &secondFunction);

  [[nodiscard]] static EF join(psr::EdgeFunctionRef<FeasibilityAddAtomsEF> thisFunc,
                               const psr::EdgeFunction<l_t> &otherFunc);

  bool operator==(const FeasibilityAddAtomsEF &o) const noexcept;
  bool isConstant() const noexcept { return false; }
};


static inline bool isIdEF(const EF &ef) noexcept {
  return ef.template isa<psr::EdgeIdentity<l_t>>();
}
static inline bool isAllTopEF(const EF &ef) noexcept {
  return ef.template isa<psr::AllTop<l_t>>();
}
static inline bool isAllBottomEF(const EF &ef) noexcept {
  return ef.template isa<FeasibilityAllBottomEF>() || ef.template isa<psr::AllBottom<l_t>>();
}

} // namespace Feasibility

#endif // SPEAR_FEASIBILITYEDGEFUNCTION_H