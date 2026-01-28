/*
 * Copyright (c) 2026 Maximilian Krebs
 * All rights reserved.
*/

#include "analyses/LoopBoundEdgeFunction.h"
#include "analyses/util.h"

namespace loopbound {


/**
 *
 * Identity Edge function implementation
 *
 */

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

    if (otherFunc.template isa<DeltaIntervalBottom>() ||
        llvm::isa<psr::AllBottom<l_t>>(otherFunc)) {
        return EF(std::in_place_type<DeltaIntervalBottom>);
        }

    if (otherFunc.template isa<DeltaIntervalTop>() ||
        llvm::isa<psr::AllTop<l_t>>(otherFunc)) {
        return EF(std::in_place_type<DeltaIntervalIdentity>);
        }

    if (otherFunc.template isa<DeltaIntervalIdentity>() ||
        otherFunc.template isa<psr::EdgeIdentity<l_t>>()) {
        return EF(std::in_place_type<DeltaIntervalIdentity>);
        }

    // Conservative: keep the other function (more specific than identity under meet)
    return otherFunc;
}


bool DeltaIntervalIdentity::isConstant() const noexcept {
    return false;
}

/**
 *
 * Bottom Edge function implementation
 * (replacement for psr::AllBottom<l_t>)
 *
 */

[[nodiscard]] DeltaIntervalBottom::l_t
DeltaIntervalBottom::computeTarget(const l_t &source) const {
    // Preserve unreachable: bottom stays bottom
    if (source.isBottom()) {
        return source;
    }
    // Everything else maps to bottom
    return l_t::bottom();
}

EF DeltaIntervalBottom::compose(psr::EdgeFunctionRef<DeltaIntervalBottom>,
                               const EF &second) {
    // second ∘ Bottom:
    // Bottom maps any non-bottom to ⊥ and preserves ⊥.
    // So result is "constant ⊥" under your family, i.e., BottomEF.
    (void)second;
    return EF(std::in_place_type<DeltaIntervalBottom>);
}


psr::EdgeFunction<DeltaIntervalBottom::l_t>
DeltaIntervalBottom::join(psr::EdgeFunctionRef<DeltaIntervalBottom>,
                          const EF & /*otherFunc*/) {
    // GLB: bottom absorbs
    return EF(std::in_place_type<DeltaIntervalBottom>);
}

bool DeltaIntervalBottom::isConstant() const noexcept {
    return true;
}

/**
 *
 * Top Edge function implementation
 *
 */

[[nodiscard]] DeltaIntervalTop::l_t
DeltaIntervalTop::computeTarget(const l_t &source) const {
    // IMPORTANT: preserve unreachable
    if (source.isBottom()) {
        return source;
    }
    return l_t::top();
}

EF DeltaIntervalTop::compose(psr::EdgeFunctionRef<DeltaIntervalTop>,
                            const EF &second) {
    // We compute: second ∘ Top
    // Top maps any non-bottom to ⊤, preserves ⊥.
    // So the result depends on what second does to ⊤ and ⊥.

    if (second.template isa<DeltaIntervalBottom>() ||
        llvm::isa<psr::AllBottom<l_t>>(second)) {
        // Bottom(⊤) = ⊥, Bottom(⊥) = ⊥  => Bottom ∘ Top = Bottom
        return EF(std::in_place_type<DeltaIntervalBottom>);
        }

    if (second.template isa<DeltaIntervalTop>() ||
        llvm::isa<psr::AllTop<l_t>>(second)) {
        return EF(std::in_place_type<DeltaIntervalTop>);
        }

    // Identity(⊤)=⊤, Normal(⊤)=⊤ (because your Normal preserves ⊤)
    // => second ∘ Top = Top for all remaining functions in your family.
    return EF(std::in_place_type<DeltaIntervalTop>);
}

psr::EdgeFunction<DeltaIntervalTop::l_t>
DeltaIntervalTop::join(psr::EdgeFunctionRef<DeltaIntervalTop>,
                       const EF &otherFunc) {
    return otherFunc;
}

bool DeltaIntervalTop::isConstant() const noexcept {
    return false;
}

/**
 *
 * Interval Edge function implementation
 *
 */

[[nodiscard]] DeltaIntervalNormal::l_t
DeltaIntervalNormal::computeTarget(const l_t &source) const {
    // Preserve unreachable
    if (source.isBottom()) {
        return source;
    }

    if (source.isTop()) {
        return l_t::top();
    }

    return l_t::interval(source.getLowerBound() + lowerBound,
                         source.getUpperBound() + upperBound);
}

EF DeltaIntervalNormal::compose(psr::EdgeFunctionRef<DeltaIntervalNormal> self,
                               const EF &second) {
    // We compute: second ∘ self

    if (second.template isa<DeltaIntervalIdentity>()) {
        return EF(self);
    }

    if (second.template isa<psr::EdgeIdentity<l_t>>()) {
        return EF(self);
    }

    if (second.template isa<DeltaIntervalBottom>()) {
        // bottom ∘ normal = bottom
        return EF(std::in_place_type<DeltaIntervalBottom>);
    }

    if (second.template isa<DeltaIntervalTop>()) {
        // top ∘ normal = top
        return EF(std::in_place_type<DeltaIntervalTop>);
    }

    if (auto *secondAsNormal = second.template dyn_cast<DeltaIntervalNormal>()) {
        // (x+[a,b]) then +[c,d] => x+[a+c, b+d]
        return EF(std::in_place_type<DeltaIntervalNormal>,
                  self->lowerBound + secondAsNormal->lowerBound,
                  self->upperBound + secondAsNormal->upperBound);
    }

    return EF(std::in_place_type<DeltaIntervalTop>);
}

EF DeltaIntervalNormal::join(psr::EdgeFunctionRef<DeltaIntervalNormal> thisFunc,
                             const EF &otherFunc) {

  if (loopbound::LB_DebugEnabled.load()) {
    llvm::errs() << loopbound::LB_TAG << " EF-join Normal  this=ADD["
                 << thisFunc->lowerBound << "," << thisFunc->upperBound
                 << "]  other=";
    loopbound::dumpEF(otherFunc);
    llvm::errs() << "\n";
  }

  // Bottom on edge-functions should be neutral/ignored for join in PhASAR's IDE
  // (it typically represents "no information/identity-like" at EF level here).
  if (otherFunc.template isa<DeltaIntervalBottom>() ||
      llvm::isa<psr::AllBottom<l_t>>(otherFunc)) {
    if (loopbound::LB_DebugEnabled.load()) {
      llvm::errs() << loopbound::LB_TAG << "   -> keep THIS (other is BOT)\n";
    }
    return EF(thisFunc);
  }

  // Top is absorbing (unknown)
  if (otherFunc.template isa<DeltaIntervalTop>() ||
      llvm::isa<psr::AllTop<l_t>>(otherFunc)) {
    if (loopbound::LB_DebugEnabled.load()) {
      llvm::errs() << loopbound::LB_TAG << "   -> TOP (other is TOP)\n";
    }
    return EF(std::in_place_type<DeltaIntervalTop>);
  }

  // Joining with Identity: cannot represent precisely in "ADD[...]" family,
  // be conservative.
  if (otherFunc.template isa<DeltaIntervalIdentity>() ||
      otherFunc.template isa<psr::EdgeIdentity<l_t>>()) {
    if (loopbound::LB_DebugEnabled.load()) {
      llvm::errs() << loopbound::LB_TAG << "   -> TOP (join with ID not representable)\n";
    }
    return EF(std::in_place_type<DeltaIntervalTop>);
  }

  if (auto *otherN = otherFunc.template dyn_cast<DeltaIntervalNormal>()) {
    // FIX 3: intersection (tighten possible increment range)
    const int64_t L = std::max(thisFunc->lowerBound, otherN->lowerBound);
    const int64_t U = std::min(thisFunc->upperBound, otherN->upperBound);

    if (L > U) {
      if (loopbound::LB_DebugEnabled.load()) {
        llvm::errs() << loopbound::LB_TAG << "   -> BOT (intersection empty)\n";
      }
      return EF(std::in_place_type<DeltaIntervalBottom>);
    }

    if (loopbound::LB_DebugEnabled.load()) {
      llvm::errs() << loopbound::LB_TAG << "   -> ADD[" << L << "," << U
                   << "] (intersection)\n";
    }
    return EF(std::in_place_type<DeltaIntervalNormal>, L, U);
  }

  // Unknown other kind => conservative
  if (loopbound::LB_DebugEnabled.load()) {
    llvm::errs() << loopbound::LB_TAG << "   -> TOP (fallback)\n";
  }
  return EF(std::in_place_type<DeltaIntervalTop>);
}

bool DeltaIntervalNormal::isConstant() const noexcept {
    return false;
}


DeltaIntervalAssign::DeltaIntervalAssign(int64_t L, int64_t U)
    : lowerBound(L), upperBound(U) {}

DeltaIntervalAssign::l_t
DeltaIntervalAssign::computeTarget(const l_t &source) const {
    if (source.isBottom()) {
        return source;
    }

    // If encoded as lower > upper => EMPTY (reset state)
    if (lowerBound > upperBound) {
        return l_t::empty();
    }

    return l_t::interval(lowerBound, upperBound);
}

loopbound::EF
DeltaIntervalAssign::compose(psr::EdgeFunctionRef<DeltaIntervalAssign> self,
                             const loopbound::EF &second) {
  // second ∘ assign

  // identity ∘ assign = assign
  if (second.template isa<DeltaIntervalIdentity>() ||
      second.template isa<psr::EdgeIdentity<l_t>>()) {
    return EF(self);
  }

  // bottom ∘ assign = bottom
  if (second.template isa<DeltaIntervalBottom>() ||
      llvm::isa<psr::AllBottom<l_t>>(second)) {
    return EF(std::in_place_type<DeltaIntervalBottom>);
  }

  // apply second to the assigned constant interval
  l_t assigned = l_t::interval(self->lowerBound, self->upperBound);
  l_t after    = second.computeTarget(assigned);

  if (after.isBottom()) {
    return EF(std::in_place_type<DeltaIntervalBottom>);
  }
  if (after.isTop()) {
    return EF(std::in_place_type<DeltaIntervalTop>);
  }

  return EF(std::in_place_type<DeltaIntervalAssign>,
            after.getLowerBound(),
            after.getUpperBound());
}

loopbound::EF
DeltaIntervalAssign::join(psr::EdgeFunctionRef<DeltaIntervalAssign> thisFunc,
                          const loopbound::EF &other) {
  // LUB / join on edge functions, consistent with "bottom = unreachable"
  // (so bottom is neutral; top is absorbing)

  // other is Bottom => keep this
    if (other.template isa<DeltaIntervalBottom>() ||
      llvm::isa<psr::AllBottom<l_t>>(other)) {
        return EF(thisFunc);
      }

  // other is Top => Top
  if (other.template isa<DeltaIntervalTop>() ||
      llvm::isa<psr::AllTop<l_t>>(other)) {
    return EF(std::in_place_type<DeltaIntervalTop>);
  }

  // Assign ⊔ Assign = constant-hull (LUB on produced constants)
  if (auto *otherA = other.template dyn_cast<DeltaIntervalAssign>()) {
    const auto &A = *thisFunc;  // DeltaIntervalAssign
    const auto &B = *otherA;    // DeltaIntervalAssign

    // LUB for concrete intervals = convex hull
    const int64_t L = std::min(A.lowerBound, B.lowerBound);
    const int64_t U = std::max(A.upperBound, B.upperBound);

    return EF(std::in_place_type<DeltaIntervalAssign>, L, U);
  }

  // Assign ⊔ Identity (or any other non-constant transformer) is not representable
  // precisely in this family -> go conservative.
  return EF(std::in_place_type<DeltaIntervalTop>);
}

bool DeltaIntervalAssign::isConstant() const noexcept {
  return true;
}


DeltaIntervalCollect::DeltaIntervalCollect(int64_t L, int64_t U)
    : lowerBound(L), upperBound(U) {}

[[nodiscard]] DeltaIntervalCollect::l_t
DeltaIntervalCollect::computeTarget(const l_t &source) const {
  // Preserve unreachable
  if (source.isBottom()) {
    return source;
  }

  // If source is TOP, we can't refine it by joining; keep TOP.
  if (source.isTop()) {
    return source;
  }

  // Join in the increment interval.
  l_t inc = l_t::interval(lowerBound, upperBound);
  return source.leastUpperBound(inc);
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

    // unknown transformer after collect => conservative
    return EF(std::in_place_type<DeltaIntervalTop>);
}

EF DeltaIntervalCollect::join(psr::EdgeFunctionRef<DeltaIntervalCollect> thisFunc,
                 const EF &other) {
    // join on edge functions must over-approximate effects

    if (other.template isa<DeltaIntervalBottom>() ||
        llvm::isa<psr::AllBottom<l_t>>(other)) {
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

    if (other.template isa<DeltaIntervalIdentity>() ||
        other.template isa<psr::EdgeIdentity<l_t>>()) {
        // join(collect, id) not representable precisely
        return EF(std::in_place_type<DeltaIntervalTop>);
        }

    return EF(std::in_place_type<DeltaIntervalTop>);
}

bool DeltaIntervalCollect::isConstant() const noexcept {
  return false;
}

} // namespace loopbound
