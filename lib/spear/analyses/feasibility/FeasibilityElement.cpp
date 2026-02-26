/*
 * Copyright (c) 2026 Maximilian Krebs
 * All rights reserved.
 */

#include <string>
#include <sstream>

#include "analyses/feasibility/FeasibilityAnalysisManager.h"
#include "analyses/feasibility/FeasibilityElement.h"

namespace Feasibility {

FeasibilityElement FeasibilityElement::createElement(FeasibilityAnalysisManager *man,
                                                    uint32_t formulaId,
                                                    Kind type,
                                                    uint32_t envId) noexcept {
  return FeasibilityElement(type, formulaId, man, envId);
}

FeasibilityElement FeasibilityElement::join(const FeasibilityElement &other) const {
  /**
   * Perform joining of two elements according to the lattice properties.
   * This is not the join of the underlying formula sets, but the join of the lattice elements representing these sets.
   * The actual join operation of the lattice is located in the edge functions.
   */

  /**
   * If either element is empty, the result is the other element (empty is neutral for join).
   * This is because the empty element represents the top element in our lattice, which is the empty set of formulas (i.e., true).
   * Joining with the empty element should not change the other element, as it does not add any constraints to the formula set.
   */
  if (isEmpty()) return other;
  if (other.isEmpty()) return *this;

  /**
   * Bottom is our error state and should dominate the join. If either element is Bottom, the result is Bottom.
   */
  if (isBottom()) return *this;
  if (other.isBottom()) return other;

  // If both elements are Normal, we need to calculate the new formula ID representing the intersection
  // of their formula sets.
  uint32_t newId = manager->intersect(formularID, other.formularID);

  // If the resulting element has the same formula ID as the Top element (empty set of formulas),
  // we return a new Empty element to represent this explicitly.
  if (newId == topId) {
    return createElement(manager, topId, Kind::Empty, 0);
  }

  return createElement(manager, newId, Kind::Normal, 0);
}

std::string FeasibilityElement::toString() const {
  std::ostringstream os;
  os << "FeasibilityElement{kind=";

  switch (kind) {
    case Kind::Bottom: os << "Bottom"; break;
    case Kind::Normal: os << "Normal"; break;
    case Kind::Empty: os << "Empty"; break;
  }

  os << ", fid=" << formularID << ", env=" << envId << "}";
  return os.str();
}

llvm::raw_ostream &operator<<(llvm::raw_ostream &os, const FeasibilityElement &E) {
  os << E.toString();
  return os;
}

std::string toString(const std::optional<FeasibilityElement> &E) {
  if (!E) return "nullopt";
  return E->toString();
}

}  // namespace Feasibility
