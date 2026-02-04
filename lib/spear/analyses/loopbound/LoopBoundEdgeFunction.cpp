/*
 * Copyright (c) 2026 Maximilian Krebs
 * All rights reserved.
*/

#include <algorithm>

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

//=========================== Additive EF ============================//

DeltaIntervalAdditive::DeltaIntervalAdditive(int64_t L, int64_t U)
    : lowerBound(L), upperBound(U) {}

[[nodiscard]] DeltaIntervalAdditive::l_t
DeltaIntervalAdditive::computeTarget(const l_t &source) const {
  if (source.isBottom()) {
    return source;
  }

  const l_t inc = l_t::interval(lowerBound, upperBound, DeltaInterval::ValueType::Additive);

  if (source.isEmpty()) {
    return inc;
  }
  if (source.isTop()) {
    return source;
  }

  return source.leastUpperBound(inc);  // hull accumulate
}

EF DeltaIntervalAdditive::compose(psr::EdgeFunctionRef<DeltaIntervalAdditive> self,
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

  if (second.template isa<DeltaIntervalMultiplicative>()) {
    return EF(std::in_place_type<DeltaIntervalTop>);
  }

  if (auto *otherC = second.template dyn_cast<DeltaIntervalAdditive>()) {
    const int64_t L = std::min(self->lowerBound, otherC->lowerBound);
    const int64_t U = std::max(self->upperBound, otherC->upperBound);
    return EF(std::in_place_type<DeltaIntervalAdditive>, L, U);
  }

  // Mixing families => conservative
  return EF(std::in_place_type<DeltaIntervalTop>);
}

EF DeltaIntervalAdditive::join(psr::EdgeFunctionRef<DeltaIntervalAdditive> thisFunc,
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

  if (other.template isa<DeltaIntervalMultiplicative>()) {
    return EF(std::in_place_type<DeltaIntervalTop>);
      }

  if (auto *otherC = other.template dyn_cast<DeltaIntervalAdditive>()) {
    const int64_t L = std::min(thisFunc->lowerBound, otherC->lowerBound);
    const int64_t U = std::max(thisFunc->upperBound, otherC->upperBound);
    return EF(std::in_place_type<DeltaIntervalAdditive>, L, U);
  }

  // Mixing Elements => conservative
  return EF(std::in_place_type<DeltaIntervalTop>);
}

bool DeltaIntervalAdditive::isConstant() const noexcept { return false; }


//=========================== Multiplicative EF ============================//

DeltaIntervalMultiplicative::DeltaIntervalMultiplicative(int64_t L, int64_t U)
    : lowerBound(L), upperBound(U) {}

[[nodiscard]] DeltaIntervalMultiplicative::l_t
DeltaIntervalMultiplicative::computeTarget(const l_t &source) const {
  if (source.isBottom()) {
    return source;
  }

  const l_t inc = l_t::interval(lowerBound, upperBound, DeltaInterval::ValueType::Multiplicative);

  if (source.isEmpty()) {
    return inc;
  }
  if (source.isTop()) {
    return source;
  }

  return source.leastUpperBound(inc);  // hull accumulate
}

EF DeltaIntervalMultiplicative::compose(psr::EdgeFunctionRef<DeltaIntervalMultiplicative> self,
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

  // Do not mix lattice element types
  if (second.template isa<DeltaIntervalAdditive>()) {
    return EF(std::in_place_type<DeltaIntervalTop>);
  }

  if (second.template isa<DeltaIntervalDivision>()) {
    return EF(std::in_place_type<DeltaIntervalTop>);
  }

  if (auto *otherC = second.template dyn_cast<DeltaIntervalMultiplicative>()) {
    const int64_t L = std::min(self->lowerBound, otherC->lowerBound);
    const int64_t U = std::max(self->upperBound, otherC->upperBound);
    return EF(std::in_place_type<DeltaIntervalMultiplicative>, L, U);
  }

  // Mixing families => conservative
  return EF(std::in_place_type<DeltaIntervalTop>);
}

EF DeltaIntervalMultiplicative::join(psr::EdgeFunctionRef<DeltaIntervalMultiplicative> thisFunc,
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

  // Do not mix lattice element types
  if (other.template isa<DeltaIntervalAdditive>()) {
    return EF(std::in_place_type<DeltaIntervalTop>);
  }

  if (other.template isa<DeltaIntervalDivision>()) {
    return EF(std::in_place_type<DeltaIntervalTop>);
  }

  if (auto *otherC = other.template dyn_cast<DeltaIntervalMultiplicative>()) {
    const int64_t L = std::min(thisFunc->lowerBound, otherC->lowerBound);
    const int64_t U = std::max(thisFunc->upperBound, otherC->upperBound);
    return EF(std::in_place_type<DeltaIntervalMultiplicative>, L, U);
  }

  // Mixing Elements => conservative
  return EF(std::in_place_type<DeltaIntervalTop>);
}

bool DeltaIntervalMultiplicative::isConstant() const noexcept { return false; }

//=========================== Division EF ============================//

DeltaIntervalDivision::DeltaIntervalDivision(int64_t L, int64_t U)
    : lowerBound(L), upperBound(U) {}

[[nodiscard]] DeltaIntervalMultiplicative::l_t
DeltaIntervalDivision::computeTarget(const l_t &source) const {
  if (source.isBottom()) {
    return source;
  }

  const l_t inc = l_t::interval(lowerBound, upperBound, DeltaInterval::ValueType::Division);

  if (source.isEmpty()) {
    return inc;
  }
  if (source.isTop()) {
    return source;
  }

  return source.leastUpperBound(inc);  // hull accumulate
}

EF DeltaIntervalDivision::compose(psr::EdgeFunctionRef<DeltaIntervalDivision> self,
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

  // Do not mix lattice element types
  if (second.template isa<DeltaIntervalAdditive>()) {
    return EF(std::in_place_type<DeltaIntervalTop>);
  }

  if (second.template isa<DeltaIntervalMultiplicative>()) {
    return EF(std::in_place_type<DeltaIntervalTop>);
  }

  if (auto *otherC = second.template dyn_cast<DeltaIntervalDivision>()) {
    const int64_t L = std::min(self->lowerBound, otherC->lowerBound);
    const int64_t U = std::max(self->upperBound, otherC->upperBound);
    return EF(std::in_place_type<DeltaIntervalDivision>, L, U);
  }

  // Mixing families => conservative
  return EF(std::in_place_type<DeltaIntervalTop>);
}

EF DeltaIntervalDivision::join(psr::EdgeFunctionRef<DeltaIntervalDivision> thisFunc,
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

  // Do not mix lattice element types
  if (other.template isa<DeltaIntervalAdditive>()) {
    return EF(std::in_place_type<DeltaIntervalTop>);
  }

  if (other.template isa<DeltaIntervalMultiplicative>()) {
    return EF(std::in_place_type<DeltaIntervalTop>);
  }

  if (auto *otherC = other.template dyn_cast<DeltaIntervalDivision>()) {
    const int64_t L = std::min(thisFunc->lowerBound, otherC->lowerBound);
    const int64_t U = std::max(thisFunc->upperBound, otherC->upperBound);
    return EF(std::in_place_type<DeltaIntervalDivision>, L, U);
  }

  // Mixing Elements => conservative
  return EF(std::in_place_type<DeltaIntervalTop>);
}

bool DeltaIntervalDivision::isConstant() const noexcept { return false; }


// =========================== Helper ======================================
EF edgeIdentity() {
  return EF(std::in_place_type<DeltaIntervalIdentity>);
}

EF edgeTop() {
  return EF(std::in_place_type<DeltaIntervalTop>);
}

}  // namespace LoopBound
