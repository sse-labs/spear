/*
 * Copyright (c) 2026 Maximilian Krebs
 * All rights reserved.
*/

#include "analyses/LoopBoundEdgeFunction.h"
#include "analyses/util.h"

namespace loopbound {

//=========================== Identity EF ===========================//

[[nodiscard]] DeltaIntervalIdentity::l_t
DeltaIntervalIdentity::computeTarget(const l_t &source) const {
  return source;
}

EF DeltaIntervalIdentity::compose(psr::EdgeFunctionRef<DeltaIntervalIdentity>,
                                 const EF &second) {
  // second ∘ id = second
  return second;
}

psr::EdgeFunction<DeltaIntervalIdentity::l_t>
DeltaIntervalIdentity::join(psr::EdgeFunctionRef<DeltaIntervalIdentity>,
                            const EF &otherFunc) {
  // Identity is neutral for join: id ⊔ f = f
  return otherFunc;
}

bool DeltaIntervalIdentity::isConstant() const noexcept { return false; }

//=========================== Bottom EF ============================//

[[nodiscard]] DeltaIntervalBottom::l_t
DeltaIntervalBottom::computeTarget(const l_t &source) const {
  if (source.isBottom()) {
    return source;
  }
  return l_t::bottom();
}

EF DeltaIntervalBottom::compose(psr::EdgeFunctionRef<DeltaIntervalBottom>,
                               const EF & /*second*/) {
  // second ∘ bottom = bottom
  return EF(std::in_place_type<DeltaIntervalBottom>);
}

psr::EdgeFunction<DeltaIntervalBottom::l_t>
DeltaIntervalBottom::join(psr::EdgeFunctionRef<DeltaIntervalBottom>,
                          const EF &otherFunc) {
  // IMPORTANT for your use-case:
  // Treat EF-bottom as "no contribution" / neutral (otherwise you kill effects).
  // So: bottom ⊔ f = f
  return otherFunc;
}

bool DeltaIntervalBottom::isConstant() const noexcept { return true; }

//============================= Top EF =============================//

[[nodiscard]] DeltaIntervalTop::l_t
DeltaIntervalTop::computeTarget(const l_t &source) const {
  if (source.isBottom()) {
    return source;
  }
  return l_t::top();
}

EF DeltaIntervalTop::compose(psr::EdgeFunctionRef<DeltaIntervalTop>,
                            const EF &second) {
  // second ∘ Top:
  // Top maps any non-bottom to ⊤, preserves ⊥.
  if (second.template isa<DeltaIntervalBottom>() ||
      llvm::isa<psr::AllBottom<l_t>>(second)) {
    return EF(std::in_place_type<DeltaIntervalBottom>);
  }
  if (second.template isa<DeltaIntervalTop>() ||
      llvm::isa<psr::AllTop<l_t>>(second)) {
    return EF(std::in_place_type<DeltaIntervalTop>);
  }
  // Any other second gets ⊤ as input => conservative is ⊤
  return EF(std::in_place_type<DeltaIntervalTop>);
}

psr::EdgeFunction<DeltaIntervalTop::l_t>
DeltaIntervalTop::join(psr::EdgeFunctionRef<DeltaIntervalTop>,
                       const EF & /*otherFunc*/) {
  // Top is absorbing for join: top ⊔ f = top
  return EF(std::in_place_type<DeltaIntervalTop>);
}

bool DeltaIntervalTop::isConstant() const noexcept { return false; }

//=========================== Normal EF ============================//
// If you *still have* Normal edges somewhere, keep it benign.
// But for the "collect increments" semantics, you ideally don't use Normal at all.

[[nodiscard]] DeltaIntervalNormal::l_t
DeltaIntervalNormal::computeTarget(const l_t &source) const {
  if (source.isBottom()) {
    return source;
  }
  if (source.isTop()) {
    return l_t::top();
  }
  // For collected increment interval semantics, "Normal" doesn't really apply,
  // but keep your old behavior if it's still used elsewhere.
  if (source.isEmpty()) {
    // Empty + c -> [c,c]
    return l_t::interval(lowerBound, upperBound);
  }
  return l_t::interval(source.getLowerBound() + lowerBound,
                       source.getUpperBound() + upperBound);
}

EF DeltaIntervalNormal::compose(psr::EdgeFunctionRef<DeltaIntervalNormal> self,
                               const EF &second) {
  // second ∘ self

  if (second.template isa<DeltaIntervalIdentity>() ||
      second.template isa<psr::EdgeIdentity<l_t>>()) {
    return EF(self);
  }

  if (second.template isa<DeltaIntervalBottom>() ||
      llvm::isa<psr::AllBottom<l_t>>(second)) {
    return EF(std::in_place_type<DeltaIntervalBottom>);
  }

  if (second.template isa<DeltaIntervalTop>() ||
      llvm::isa<psr::AllTop<l_t>>(second)) {
    return EF(std::in_place_type<DeltaIntervalTop>);
  }

  if (auto *secondAsNormal = second.template dyn_cast<DeltaIntervalNormal>()) {
    return EF(std::in_place_type<DeltaIntervalNormal>,
              self->lowerBound + secondAsNormal->lowerBound,
              self->upperBound + secondAsNormal->upperBound);
  }

  // Mixing families => conservative
  return EF(std::in_place_type<DeltaIntervalTop>);
}

EF DeltaIntervalNormal::join(psr::EdgeFunctionRef<DeltaIntervalNormal> thisFunc,
                            const EF &otherFunc) {
  if (LB_DebugEnabled.load()) {
    llvm::errs() << LB_TAG << " EF-join Normal  this=ADD["
                 << thisFunc->lowerBound << "," << thisFunc->upperBound
                 << "]  other=";
    dumpEF(otherFunc);
    llvm::errs() << "\n";
  }

  // EF-bottom is neutral
  if (otherFunc.template isa<DeltaIntervalBottom>() ||
      llvm::isa<psr::AllBottom<l_t>>(otherFunc)) {
    if (LB_DebugEnabled.load()) {
      llvm::errs() << LB_TAG << "   -> keep THIS (other is BOT)\n";
    }
    return EF(thisFunc);
  }

  // EF-identity is neutral (THIS IS THE IMPORTANT FIX)
  if (otherFunc.template isa<DeltaIntervalIdentity>() ||
      otherFunc.template isa<psr::EdgeIdentity<l_t>>()) {
    if (LB_DebugEnabled.load()) {
      llvm::errs() << LB_TAG << "   -> keep THIS (other is ID)\n";
    }
    return EF(thisFunc);
  }

  // Top is absorbing
  if (otherFunc.template isa<DeltaIntervalTop>() ||
      llvm::isa<psr::AllTop<l_t>>(otherFunc)) {
    if (LB_DebugEnabled.load()) {
      llvm::errs() << LB_TAG << "   -> TOP (other is TOP)\n";
    }
    return EF(std::in_place_type<DeltaIntervalTop>);
  }

  if (auto *otherN = otherFunc.template dyn_cast<DeltaIntervalNormal>()) {
    // Use intersection only if you *really* mean "must increment by both".
    // Usually for "possible increments" you want hull. Keep intersection only if intended.
    const int64_t L = std::max(thisFunc->lowerBound, otherN->lowerBound);
    const int64_t U = std::min(thisFunc->upperBound, otherN->upperBound);
    if (L > U) {
      if (LB_DebugEnabled.load()) {
        llvm::errs() << LB_TAG << "   -> BOT (intersection empty)\n";
      }
      return EF(std::in_place_type<DeltaIntervalBottom>);
    }
    if (LB_DebugEnabled.load()) {
      llvm::errs() << LB_TAG << "   -> ADD[" << L << "," << U << "]\n";
    }
    return EF(std::in_place_type<DeltaIntervalNormal>, L, U);
  }

  // Mixing families => conservative
  if (LB_DebugEnabled.load()) {
    llvm::errs() << LB_TAG << "   -> TOP (fallback)\n";
  }
  return EF(std::in_place_type<DeltaIntervalTop>);
}

bool DeltaIntervalNormal::isConstant() const noexcept { return false; }

//=========================== Assign EF ============================//

DeltaIntervalAssign::DeltaIntervalAssign(int64_t L, int64_t U)
    : lowerBound(L), upperBound(U) {}

DeltaIntervalAssign::l_t
DeltaIntervalAssign::computeTarget(const l_t &source) const {
  if (source.isBottom()) {
    return source;
  }
  if (lowerBound > upperBound) {
    return l_t::empty();
  }
  return l_t::interval(lowerBound, upperBound);
}

EF DeltaIntervalAssign::compose(psr::EdgeFunctionRef<DeltaIntervalAssign> self,
                               const EF &second) {
  // second ∘ assign
  if (second.template isa<DeltaIntervalIdentity>() ||
      second.template isa<psr::EdgeIdentity<l_t>>()) {
    return EF(self);
  }
  if (second.template isa<DeltaIntervalBottom>() ||
      llvm::isa<psr::AllBottom<l_t>>(second)) {
    return EF(std::in_place_type<DeltaIntervalBottom>);
  }
  if (second.template isa<DeltaIntervalTop>() ||
      llvm::isa<psr::AllTop<l_t>>(second)) {
    return EF(std::in_place_type<DeltaIntervalTop>);
  }

  l_t assigned = l_t::interval(self->lowerBound, self->upperBound);
  l_t after = second.computeTarget(assigned);

  if (after.isBottom()) return EF(std::in_place_type<DeltaIntervalBottom>);
  if (after.isTop())    return EF(std::in_place_type<DeltaIntervalTop>);
  if (after.isEmpty())  return EF(std::in_place_type<DeltaIntervalAssign>, 1, 0);

  return EF(std::in_place_type<DeltaIntervalAssign>,
            after.getLowerBound(), after.getUpperBound());
}

EF DeltaIntervalAssign::join(psr::EdgeFunctionRef<DeltaIntervalAssign> thisFunc,
                            const EF &other) {
  // bottom/identity neutral
  if (other.template isa<DeltaIntervalBottom>() ||
      llvm::isa<psr::AllBottom<l_t>>(other) ||
      other.template isa<DeltaIntervalIdentity>() ||
      other.template isa<psr::EdgeIdentity<l_t>>()) {
    return EF(thisFunc);
  }

  if (other.template isa<DeltaIntervalTop>() ||
      llvm::isa<psr::AllTop<l_t>>(other)) {
    return EF(std::in_place_type<DeltaIntervalTop>);
  }

  if (auto *otherA = other.template dyn_cast<DeltaIntervalAssign>()) {
    const int64_t L = std::min(thisFunc->lowerBound, otherA->lowerBound);
    const int64_t U = std::max(thisFunc->upperBound, otherA->upperBound);
    return EF(std::in_place_type<DeltaIntervalAssign>, L, U);
  }

  return EF(std::in_place_type<DeltaIntervalTop>);
}

bool DeltaIntervalAssign::isConstant() const noexcept { return true; }

//=========================== Collect EF ============================//

DeltaIntervalCollect::DeltaIntervalCollect(int64_t L, int64_t U)
    : lowerBound(L), upperBound(U) {}

[[nodiscard]] DeltaIntervalCollect::l_t
DeltaIntervalCollect::computeTarget(const l_t &source) const {
  if (source.isBottom()) {
    return source;
  }

  const l_t inc = l_t::interval(lowerBound, upperBound);

  if (source.isEmpty()) {
    return inc;
  }
  if (source.isTop()) {
    return source;
  }
  return source.leastUpperBound(inc); // hull accumulate
}

EF DeltaIntervalCollect::compose(psr::EdgeFunctionRef<DeltaIntervalCollect> self,
                                const EF &second) {
  // second ∘ collect

  if (second.template isa<DeltaIntervalIdentity>() ||
      second.template isa<psr::EdgeIdentity<l_t>>()) {
    return EF(self);
  }

  if (second.template isa<DeltaIntervalBottom>() ||
      llvm::isa<psr::AllBottom<l_t>>(second)) {
    return EF(std::in_place_type<DeltaIntervalBottom>);
  }

  if (second.template isa<DeltaIntervalTop>() ||
      llvm::isa<psr::AllTop<l_t>>(second)) {
    return EF(std::in_place_type<DeltaIntervalTop>);
  }

  if (auto *otherC = second.template dyn_cast<DeltaIntervalCollect>()) {
    const int64_t L = std::min(self->lowerBound, otherC->lowerBound);
    const int64_t U = std::max(self->upperBound, otherC->upperBound);
    return EF(std::in_place_type<DeltaIntervalCollect>, L, U);
  }

  // Mixing families => conservative
  return EF(std::in_place_type<DeltaIntervalTop>);
}

EF DeltaIntervalCollect::join(psr::EdgeFunctionRef<DeltaIntervalCollect> thisFunc,
                             const EF &other) {
  // IMPORTANT: Identity and Bottom are neutral here.
  if (other.template isa<DeltaIntervalBottom>() ||
      llvm::isa<psr::AllBottom<l_t>>(other) ||
      other.template isa<DeltaIntervalIdentity>() ||
      other.template isa<psr::EdgeIdentity<l_t>>()) {
    return EF(thisFunc);
  }

  if (other.template isa<DeltaIntervalTop>() ||
      llvm::isa<psr::AllTop<l_t>>(other)) {
    return EF(std::in_place_type<DeltaIntervalTop>);
  }

  if (auto *otherC = other.template dyn_cast<DeltaIntervalCollect>()) {
    const int64_t L = std::min(thisFunc->lowerBound, otherC->lowerBound);
    const int64_t U = std::max(thisFunc->upperBound, otherC->upperBound);
    return EF(std::in_place_type<DeltaIntervalCollect>, L, U);
  }

  // Mixing families => conservative
  return EF(std::in_place_type<DeltaIntervalTop>);
}

bool DeltaIntervalCollect::isConstant() const noexcept { return false; }

} // namespace loopbound