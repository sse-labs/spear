// /*
//  * Copyright (c) 2026 Maximilian Krebs
//  * All rights reserved.
// *

#include "analyses/loopbound/LoopBoundEdgeFunction.h"
#include "analyses/loopbound/util.h"

namespace LoopBound {

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
  return EF(std::in_place_type<DeltaIntervalBottom>);
}

psr::EdgeFunction<DeltaIntervalBottom::l_t>
DeltaIntervalBottom::join(psr::EdgeFunctionRef<DeltaIntervalBottom>,
                          const EF &otherFunc) {
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
  return EF(std::in_place_type<DeltaIntervalTop>);
}

bool DeltaIntervalTop::isConstant() const noexcept { return false; }

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


EF edgeIdentity() {
  return EF(std::in_place_type<DeltaIntervalIdentity>);
}

EF edgeTop() {
  return EF(std::in_place_type<DeltaIntervalTop>);
}

} // namespace loopbound